#include "pch.h"

#include "F4TProbeFlush.h"

#include "F4TGamePaths.h"
#include "F4TProbeCapture.h"
#include "F4TProbeRecord.h"
#include "F4TProbeRegistry.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <intrin.h>
#include <span>
#include <vector>

namespace
{
	constexpr const char* FileName = "CommonwealthOnlineProbe.bin";
	constexpr const char* RotatedName = "CommonwealthOnlineProbe.1.bin";
	constexpr std::uintmax_t MaxFileBytes = 8 * 1024 * 1024;
	constexpr std::uint32_t FormatVersion = 1;
	constexpr std::chrono::milliseconds FlushInterval{ 1000 };
	constexpr std::array<std::byte, 4> FileMagic{ std::byte{ 'P' }, std::byte{ 'R' }, std::byte{ 'B' }, std::byte{ '1' } };
	constexpr std::array<std::byte, 4> BlockMagic{ std::byte{ 'B' }, std::byte{ 'L' }, std::byte{ 'C' }, std::byte{ 'K' } };

	std::filesystem::path g_path;
	std::filesystem::path g_rotatedPath;
	std::vector<F4T::ProbeRecord::Record> g_drain;
	std::vector<std::byte> g_payload;
	std::uint64_t g_written = 0;
	std::chrono::steady_clock::time_point g_lastFlush{};

	void PutLittleEndian(std::ofstream& a_out, std::uint64_t a_value, std::size_t a_width)
	{
		std::array<char, 8> bytes{};
		for (std::size_t index = 0; index < a_width; ++index) {
			bytes[index] = static_cast<char>((a_value >> (8 * index)) & 0xFFU);
		}
		a_out.write(bytes.data(), static_cast<std::streamsize>(a_width));
	}

	void WriteFileHeader(std::ofstream& a_out)
	{
		a_out.write(reinterpret_cast<const char*>(FileMagic.data()), static_cast<std::streamsize>(FileMagic.size()));
		PutLittleEndian(a_out, FormatVersion, 4);
	}

	void WriteBlock(std::ofstream& a_out, std::span<const F4T::ProbeRecord::Record> a_records, std::uint64_t a_overwritten)
	{
		g_payload.clear();
		std::array<std::byte, F4T::ProbeRecord::WireSize> encoded{};
		for (const auto& record : a_records) {
			if (F4T::ProbeRecord::Encode(record, encoded) != F4T::ProbeRecord::WireSize) {
				continue;
			}
			g_payload.insert(g_payload.end(), encoded.begin(), encoded.end());
		}
		const auto count = g_payload.size() / F4T::ProbeRecord::WireSize;
		a_out.write(reinterpret_cast<const char*>(BlockMagic.data()), static_cast<std::streamsize>(BlockMagic.size()));
		PutLittleEndian(a_out, count, 4);
		a_out.write(reinterpret_cast<const char*>(g_payload.data()), static_cast<std::streamsize>(g_payload.size()));
		PutLittleEndian(a_out, a_overwritten, 8);
	}

	void RotateIfFull()
	{
		std::error_code error;
		const auto size = std::filesystem::file_size(g_path, error);
		if (error || size < MaxFileBytes) {
			return;
		}
		std::filesystem::remove(g_rotatedPath, error);
		std::filesystem::rename(g_path, g_rotatedPath, error);
	}

	void Flush()
	{
		const auto pushed = F4T::ProbeCapture::PushedCount();
		const auto drained = F4T::ProbeCapture::Ring().Drain(g_drain);
		if (pushed <= g_written || drained == 0) {
			return;
		}

		const auto pending = static_cast<std::size_t>(std::min<std::uint64_t>(pushed - g_written, drained));
		const auto first = drained - pending;

		RotateIfFull();
		std::error_code error;
		const auto existingSize = std::filesystem::file_size(g_path, error);
		const auto needsHeader = error || existingSize == 0;
		std::ofstream output(g_path, std::ios::binary | std::ios::app);
		if (!output) {
			REX::WARN("Commonwealth Online probe: cannot open {} for appending", g_path.string());
			return;
		}

		if (needsHeader) {
			WriteFileHeader(output);
		}
		WriteBlock(output, std::span<const F4T::ProbeRecord::Record>{ g_drain.data() + first, pending }, F4T::ProbeCapture::Ring().Overwritten());
		if (!output) {
			REX::WARN("Commonwealth Online probe: recording to {} failed", g_path.string());
			return;
		}
		g_written = pushed;
	}
}

namespace F4T::ProbeFlush
{
	void Start()
	{
		std::filesystem::path folder;
		if (!F4T::GamePaths::ResolveF4SEFolder(folder)) {
			REX::WARN("Commonwealth Online probe: cannot resolve the F4SE folder; records will not be written");
			return;
		}

		g_path = folder / FileName;
		g_rotatedPath = folder / RotatedName;
		g_drain.resize(F4T::ProbeRing::Capacity);
		F4T::ProbeCapture::SetModuleBase(REX::FModule::GetExecutingModule().GetBaseAddress());
		g_lastFlush = std::chrono::steady_clock::now();
		REX::INFO("Commonwealth Online probe: flushing to {}", g_path.string());
	}

	void Tick()
	{
		if (g_drain.empty()) {
			return;
		}
		const auto now = std::chrono::steady_clock::now();
		if (now - g_lastFlush < FlushInterval) {
			return;
		}
		g_lastFlush = now;

		if (F4T::ProbeRegistry::ShouldRecord(F4T::ProbeRegistry::SelfTestProbeId)) {
			F4T::ProbeCapture::Push(
				F4T::ProbeRegistry::SelfTestProbeId,
				F4T::ProbeRecord::Kind::Snapshot,
				reinterpret_cast<std::uintptr_t>(_ReturnAddress()),
				{},
				0);
		}
		Flush();
	}
}
