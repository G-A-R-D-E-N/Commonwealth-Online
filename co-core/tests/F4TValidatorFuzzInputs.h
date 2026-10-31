#pragma once

#include "F4TGnsSnapshotEnvelope.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <iterator>
#include <limits>
#include <random>
#include <string>
#include <string_view>
#include <vector>

namespace ValidatorFuzz
{
	using Json = nlohmann::json;

	inline constexpr std::size_t kMaximumNestingDepth = 200;
	inline constexpr double kUint32Max = 4294967295.0;
	inline constexpr std::string_view kHexAlphabet = "0123456789abcdefABCDEFgG -+x";
	inline constexpr std::string_view kClockAlphabet = "0123456789:x -+";
	inline constexpr std::string_view kGarbageAlphabet = "{}[]\",:\\N-.e9 ";
	inline constexpr std::string_view kRawTokens[] = {
		"NaN", "Infinity", "-Infinity", "1e400", "-0", "1E+2", "18446744073709551616",
		"4294967296", "-1e-400", "true", "null", "[]", "{}"
	};
	inline constexpr std::string_view kExtraKeys[] = {
		"type", "unknown", "npcs", "npcId", "x", "sourceFormId", "cellId", "characterName",
		"clientTime", "movementType", "", "weatherFormId", "sequence", "__proto__"
	};

	struct Rng
	{
		std::mt19937_64 engine;

		std::size_t Below(std::size_t a_bound)
		{
			return static_cast<std::size_t>(engine() % a_bound);
		}
	};

	inline std::string HexWord(std::uint32_t a_value)
	{
		char buffer[9]{};
		std::snprintf(buffer, sizeof(buffer), "%08X", a_value);
		return buffer;
	}

	inline double RandomDouble(Rng& a_rng)
	{
		switch (a_rng.Below(12)) {
		case 0:
			return std::numeric_limits<double>::quiet_NaN();
		case 1:
			return std::numeric_limits<double>::infinity();
		case 2:
			return -std::numeric_limits<double>::infinity();
		case 3:
			return -0.0;
		case 4:
			return std::numeric_limits<double>::max();
		case 5:
			return -10000001.0;
		case 6:
			return 4294967296.0;
		case 7:
			return 10000000.0;
		default:
			return static_cast<double>(a_rng.Below(2000001)) / 1000.0 - 1000.0;
		}
	}

	inline double RandomSeconds(Rng& a_rng)
	{
		return a_rng.Below(4) == 0 ? RandomDouble(a_rng) : static_cast<double>(a_rng.Below(20000)) / 1000.0;
	}

	inline std::string RandomText(Rng& a_rng)
	{
		constexpr std::string_view kWords[] = {
			"", "0", "+1", " 1", "-1", "a\"b", "\\", "normal", "teleport", "fast_travel", "load", "spawn",
			"meleeattackStart", "fireSingle", "rightHand", "transform", "worldState", "12:34", "zz"
		};
		switch (a_rng.Below(8)) {
		case 0:
			return std::string("A\0B", 3);
		case 1: {
			std::string text(a_rng.Below(12), '0');
			for (auto& ch : text) {
				ch = kHexAlphabet[a_rng.Below(kHexAlphabet.size())];
			}
			return text;
		}
		case 2:
			return HexWord(static_cast<std::uint32_t>(a_rng.engine()));
		case 3:
			return HexWord(static_cast<std::uint32_t>(kUint32Max));
		case 4:
			return std::string(16, 'F');
		default:
			return std::string{ kWords[a_rng.Below(std::size(kWords))] };
		}
	}

	inline Json RandomScalar(Rng& a_rng)
	{
		switch (a_rng.Below(22)) {
		case 0:
			return nullptr;
		case 1:
			return true;
		case 2:
			return false;
		case 3:
			return 0;
		case 4:
			return -1;
		case 5:
			return -0.0;
		case 6:
			return 4294967295U;
		case 7:
			return 4294967296ULL;
		case 8:
			return std::numeric_limits<std::uint64_t>::max();
		case 9:
			return std::numeric_limits<std::int64_t>::min();
		case 10:
			return std::numeric_limits<double>::quiet_NaN();
		case 11:
			return std::numeric_limits<double>::infinity();
		case 12:
			return -std::numeric_limits<double>::infinity();
		case 13:
			return std::numeric_limits<double>::denorm_min();
		case 14:
			return 1.0e12;
		case 15:
			return RandomDouble(a_rng);
		case 16:
		case 17:
			return RandomText(a_rng);
		case 18:
			return std::string(1 + a_rng.Below(4096), 'A');
		case 19:
			return Json::array();
		case 20:
			return Json::object();
		default:
			return static_cast<std::int64_t>(a_rng.Below(70000)) - 1;
		}
	}

	inline Json DeepArray(Rng& a_rng)
	{
		Json value = RandomScalar(a_rng);
		for (std::size_t depth = 1 + a_rng.Below(kMaximumNestingDepth); depth > 0; --depth) {
			Json wrapped = Json::array();
			wrapped.push_back(std::move(value));
			value = std::move(wrapped);
		}
		return value;
	}

	inline Json* PickNode(Json& a_root, Rng& a_rng)
	{
		auto* node = &a_root;
		while (node->is_structured() && !node->empty() && a_rng.Below(3) != 0) {
			if (node->is_object()) {
				node = &*std::next(node->begin(), static_cast<std::ptrdiff_t>(a_rng.Below(node->size())));
			} else {
				node = &node->at(a_rng.Below(node->size()));
			}
		}
		return node;
	}

	inline std::string KeyAt(const Json& a_object, Rng& a_rng)
	{
		return std::next(a_object.begin(), static_cast<std::ptrdiff_t>(a_rng.Below(a_object.size()))).key();
	}

	inline void MutateOnce(Json& a_root, Rng& a_rng)
	{
		auto& node = *PickNode(a_root, a_rng);
		switch (a_rng.Below(9)) {
		case 0:
			node = RandomScalar(a_rng);
			break;
		case 1:
			node = DeepArray(a_rng);
			break;
		case 2:
			if (node.is_object() && !node.empty()) {
				node.erase(KeyAt(node, a_rng));
			}
			break;
		case 3:
			if (node.is_array() && !node.empty()) {
				node.erase(a_rng.Below(node.size()));
			}
			break;
		case 4:
			if (node.is_array() && !node.empty()) {
				Json copy = node.at(a_rng.Below(node.size()));
				node.push_back(std::move(copy));
			}
			break;
		case 5:
			if (node.is_array()) {
				for (std::size_t count = a_rng.Below(150); count > 0; --count) {
					node.push_back(RandomScalar(a_rng));
				}
			}
			break;
		case 6:
			if (node.is_object()) {
				node[std::string{ kExtraKeys[a_rng.Below(std::size(kExtraKeys))] }] = RandomScalar(a_rng);
			}
			break;
		case 7:
			if (node.is_object() && !node.empty()) {
				Json copy = node.at(KeyAt(node, a_rng));
				node[std::string{ kExtraKeys[a_rng.Below(std::size(kExtraKeys))] }] = std::move(copy);
			}
			break;
		default:
			node = RandomText(a_rng);
			break;
		}
	}

	inline void MutateText(std::string& a_text, Rng& a_rng)
	{
		switch (a_rng.Below(6)) {
		case 0:
			if (!a_text.empty()) {
				a_text.resize(a_rng.Below(a_text.size()));
			}
			break;
		case 1:
			a_text.insert(a_rng.Below(a_text.size() + 1), 1, kGarbageAlphabet[a_rng.Below(kGarbageAlphabet.size())]);
			break;
		case 2:
			a_text.insert(a_rng.Below(a_text.size() + 1), std::string{ kRawTokens[a_rng.Below(std::size(kRawTokens))] });
			break;
		case 3:
			if (!a_text.empty() && a_text.back() == '}') {
				a_text.insert(a_text.size() - 1, ",\"type\":\"welcome\"");
			}
			break;
		case 4:
			a_text.insert(0, std::string(1 + a_rng.Below(256), '['));
			break;
		default:
			break;
		}
	}

	inline std::string ClockText(Rng& a_rng)
	{
		if (a_rng.Below(3) == 0) {
			char buffer[8]{};
			std::snprintf(buffer, sizeof(buffer), "%02zu%02zu", a_rng.Below(30), a_rng.Below(70));
			return buffer;
		}
		std::string text(a_rng.Below(7), '0');
		for (auto& ch : text) {
			ch = kClockAlphabet[a_rng.Below(kClockAlphabet.size())];
		}
		return text;
	}

	inline std::uint8_t RandomByte(Rng& a_rng)
	{
		constexpr std::uint8_t kInteresting[] = { 0, 1, 2, static_cast<std::uint8_t>('C'), 255 };
		return a_rng.Below(2) == 0 ? kInteresting[a_rng.Below(std::size(kInteresting))]
		                           : static_cast<std::uint8_t>(a_rng.engine() & 255U);
	}

	inline std::vector<std::uint8_t> EnvelopeSeed(Rng& a_rng)
	{
		std::vector<std::uint8_t> bytes(F4T::GnsSnapshotEnvelope::HeaderSize + a_rng.Below(33));
		for (std::size_t index = F4T::GnsSnapshotEnvelope::HeaderSize; index < bytes.size(); ++index) {
			bytes[index] = RandomByte(a_rng);
		}
		const auto family = a_rng.Below(2) == 0 ? F4T::GnsSnapshotEnvelope::Family::Transform
		                                       : F4T::GnsSnapshotEnvelope::Family::NpcState;
		const auto sequence = static_cast<std::uint32_t>(1 + a_rng.Below(100000));
		F4T::GnsSnapshotEnvelope::WriteHeader(bytes, family, sequence);
		return bytes;
	}

	inline void MutateBytes(std::vector<std::uint8_t>& a_bytes, Rng& a_rng)
	{
		switch (a_rng.Below(5)) {
		case 0:
			a_bytes.resize(a_rng.Below(a_bytes.size() + 1));
			break;
		case 1:
			if (!a_bytes.empty()) {
				a_bytes[a_rng.Below(a_bytes.size())] = RandomByte(a_rng);
			}
			break;
		case 2:
			if (a_bytes.size() >= F4T::GnsSnapshotEnvelope::HeaderSize) {
				a_bytes[a_rng.Below(F4T::GnsSnapshotEnvelope::HeaderSize)] = RandomByte(a_rng);
			}
			break;
		case 3:
			for (std::size_t count = 1 + a_rng.Below(16); count > 0; --count) {
				a_bytes.push_back(RandomByte(a_rng));
			}
			break;
		case 4:
			a_bytes.assign(a_rng.Below(24), 0);
			for (auto& byte : a_bytes) {
				byte = RandomByte(a_rng);
			}
			break;
		}
	}
}
