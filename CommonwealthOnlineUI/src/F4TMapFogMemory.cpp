#include "pch.h"

#include "F4TMapFogMemory.h"

#include <array>
#include <cstddef>
#include <cstring>
#include <functional>
#include <map>
#include <mutex>
#include <string>
#include <string_view>
#include <utility>

namespace F4T::MapFogMemory
{
	namespace
	{
		constexpr std::uint32_t kRecordVersion = 1;
		constexpr std::size_t kCellCount = static_cast<std::size_t>(kGridSize) * kGridSize;
		constexpr std::size_t kGridBytes = kCellCount / 8;
		constexpr std::size_t kMaxKeyLength = 32;
		constexpr std::size_t kMaxRecordBytes = 16 * (1 + kMaxKeyLength + kGridBytes);
		constexpr float kTrailRadiusPct = 2.5F;
		constexpr char kBase64Alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

		struct Grid
		{
			std::array<std::uint8_t, kGridBytes> bits{};
			std::string base64;
			bool dirty{ true };
		};

		std::mutex g_mutex;
		std::map<std::string, Grid, std::less<>> g_grids;

		std::string EncodeBase64(const std::array<std::uint8_t, kGridBytes>& a_bytes)
		{
			std::string out;
			out.reserve((a_bytes.size() + 2) / 3 * 4);
			for (std::size_t i = 0; i < a_bytes.size(); i += 3) {
				const std::size_t left = a_bytes.size() - i;
				const std::uint32_t b0 = a_bytes[i];
				const std::uint32_t b1 = left > 1 ? a_bytes[i + 1] : 0U;
				const std::uint32_t b2 = left > 2 ? a_bytes[i + 2] : 0U;
				const std::uint32_t triple = (b0 << 16) | (b1 << 8) | b2;
				out.push_back(kBase64Alphabet[(triple >> 18) & 0x3FU]);
				out.push_back(kBase64Alphabet[(triple >> 12) & 0x3FU]);
				out.push_back(left > 1 ? kBase64Alphabet[(triple >> 6) & 0x3FU] : '=');
				out.push_back(left > 2 ? kBase64Alphabet[triple & 0x3FU] : '=');
			}
			return out;
		}

		bool Restore(std::string_view a_payload)
		{
			std::map<std::string, Grid, std::less<>> grids;
			std::size_t offset = 0;
			while (offset < a_payload.size()) {
				const auto keyLength = static_cast<std::uint8_t>(a_payload[offset]);
				++offset;
				if (keyLength == 0 || keyLength > kMaxKeyLength || a_payload.size() - offset < keyLength + kGridBytes) {
					return false;
				}
				std::string key(a_payload.substr(offset, keyLength));
				offset += keyLength;
				Grid grid;
				std::memcpy(grid.bits.data(), a_payload.data() + offset, kGridBytes);
				offset += kGridBytes;
				if (!grids.emplace(std::move(key), std::move(grid)).second) {
					return false;
				}
			}
			std::scoped_lock lock{ g_mutex };
			g_grids = std::move(grids);
			return true;
		}
	}

	void Clear()
	{
		std::scoped_lock lock{ g_mutex };
		g_grids.clear();
	}

	void MarkExplored(std::string_view a_mapKey, float a_xPct, float a_yPct)
	{
		if (a_mapKey.empty() || a_mapKey.size() > kMaxKeyLength) {
			return;
		}
		if (!(a_xPct >= 0.0F && a_xPct < 100.0F && a_yPct >= 0.0F && a_yPct < 100.0F)) {
			return;
		}
		constexpr float radiusCells = kTrailRadiusPct * kGridSize / 100.0F;
		const auto reach = static_cast<int>(radiusCells);
		const auto centerColumn = static_cast<int>(a_xPct * kGridSize / 100.0F);
		const auto centerRow = static_cast<int>(a_yPct * kGridSize / 100.0F);

		std::scoped_lock lock{ g_mutex };
		auto& grid = g_grids[std::string(a_mapKey)];
		for (int dy = -reach; dy <= reach; ++dy) {
			for (int dx = -reach; dx <= reach; ++dx) {
				if (static_cast<float>(dx * dx + dy * dy) > radiusCells * radiusCells) {
					continue;
				}
				const auto row = static_cast<std::size_t>(centerRow + dy);
				const auto column = static_cast<std::size_t>(centerColumn + dx);
				const auto cell = row * kGridSize + column;
				if (row >= kGridSize || column >= kGridSize || cell >= kCellCount) {
					continue;
				}
				const auto mask = static_cast<std::uint8_t>(1U << (cell % 8));
				if (!(grid.bits[cell / 8] & mask)) {
					grid.bits[cell / 8] |= mask;
					grid.dirty = true;
				}
			}
		}
	}

	std::string ExploredBase64(std::string_view a_mapKey)
	{
		std::scoped_lock lock{ g_mutex };
		const auto found = g_grids.find(a_mapKey);
		if (found == g_grids.end()) {
			return {};
		}
		auto& grid = found->second;
		if (grid.dirty) {
			grid.base64 = EncodeBase64(grid.bits);
			grid.dirty = false;
		}
		return grid.base64;
	}

	void Save(const F4SE::SerializationInterface* a_intfc)
	{
		std::string payload;
		{
			std::scoped_lock lock{ g_mutex };
			for (const auto& [key, grid] : g_grids) {
				payload.push_back(static_cast<char>(key.size()));
				payload.append(key);
				payload.append(reinterpret_cast<const char*>(grid.bits.data()), grid.bits.size());
			}
		}
		if (!a_intfc->WriteRecord(kRecordType, kRecordVersion, payload.data(), static_cast<std::uint32_t>(payload.size()))) {
			REX::WARN("[MapFog] the explored-map record could not be written; this save keeps no explored map");
		}
	}

	void Load(const F4SE::SerializationInterface* a_intfc, std::uint32_t a_version, std::uint32_t a_length)
	{
		Clear();
		if (a_version != kRecordVersion) {
			return;
		}
		if (a_length > kMaxRecordBytes) {
			REX::WARN("[MapFog] the explored-map record is too large; this load starts with no explored map");
			return;
		}
		std::string payload(a_length, '\0');
		if (a_intfc->ReadRecordData(payload.data(), a_length) != a_length || !Restore(payload)) {
			REX::WARN("[MapFog] the explored-map record was not read; this load starts with no explored map");
		}
	}

	inline constexpr const char* kMapFogRecordTag = "Lh8Xb4Ce1Wm6Vt9Ua3Ro0Ki";
	static_assert(kMapFogRecordTag != nullptr);
}
