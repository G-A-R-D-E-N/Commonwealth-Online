#include "F4TBa2Inspect.h"

#include <array>
#include <fstream>
#include <string_view>
#include <system_error>

namespace F4T::Ba2Inspect
{
	namespace
	{
		constexpr std::uint64_t kHeaderBytes = 24;
		constexpr std::array<std::string_view, 9> kRefusedExtensions{ ".dll", ".exe", ".ini", ".bat", ".cmd", ".ps1", ".vbs", ".com", ".scr" };

		template <class T>
		[[nodiscard]] bool ReadLittle(std::istream& a_stream, T& a_value)
		{
			std::array<unsigned char, sizeof(T)> bytes{};
			a_stream.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
			if (a_stream.gcount() != static_cast<std::streamsize>(bytes.size())) {
				return false;
			}
			a_value = 0;
			for (std::size_t index = 0; index < bytes.size(); ++index) {
				a_value |= static_cast<T>(static_cast<T>(bytes[index]) << (8 * index));
			}
			return true;
		}

		[[nodiscard]] std::string Lower(std::string_view a_text)
		{
			std::string lowered{ a_text };
			for (auto& character : lowered) {
				if (character >= 'A' && character <= 'Z') {
					character = static_cast<char>(character - 'A' + 'a');
				} else if (character == '/') {
					character = '\\';
				}
			}
			return lowered;
		}
	}

	bool IsRefusedInnerPath(std::string_view a_path)
	{
		if (a_path.empty() || a_path.front() == '\\' || a_path.front() == '/' || a_path.find(':') != std::string_view::npos) {
			return true;
		}
		for (const auto character : a_path) {
			const auto byte = static_cast<unsigned char>(character);
			if (byte < 0x20 || byte == 0x7F) {
				return true;
			}
		}
		const auto lowered = Lower(a_path);
		std::size_t start = 0;
		while (start <= lowered.size()) {
			const auto end = lowered.find('\\', start);
			const auto segment = std::string_view{ lowered }.substr(start, end == std::string::npos ? std::string::npos : end - start);
			if (segment == "..") {
				return true;
			}
			if (end == std::string::npos) {
				break;
			}
			start = end + 1;
		}
		if (lowered.starts_with("f4se\\")) {
			return true;
		}
		for (const auto extension : kRefusedExtensions) {
			if (lowered.ends_with(extension)) {
				return true;
			}
		}
		return false;
	}

	std::optional<Report> Inspect(std::istream& a_stream, std::uint64_t a_sizeBytes)
	{
		try {
			if (a_sizeBytes < kHeaderBytes) {
				return std::nullopt;
			}
			std::array<char, 4> magic{};
			a_stream.read(magic.data(), 4);
			if (a_stream.gcount() != 4 || std::string_view{ magic.data(), 4 } != "BTDX") {
				return std::nullopt;
			}
			Report report;
			std::array<char, 4> type{};
			std::uint64_t nameTableOffset = 0;
			if (!ReadLittle(a_stream, report.version)) {
				return std::nullopt;
			}
			a_stream.read(type.data(), 4);
			if (a_stream.gcount() != 4 || !ReadLittle(a_stream, report.fileCount) || !ReadLittle(a_stream, nameTableOffset)) {
				return std::nullopt;
			}
			const std::string_view typeText{ type.data(), 4 };
			if (typeText == "GNRL") {
				report.type = ArchiveType::general;
			} else if (typeText == "DX10") {
				report.type = ArchiveType::textures;
			} else {
				return std::nullopt;
			}
			if (report.fileCount > kMaximumFiles || nameTableOffset < kHeaderBytes || nameTableOffset > a_sizeBytes ||
				(a_sizeBytes - nameTableOffset) / 2 < report.fileCount) {
				return std::nullopt;
			}
			a_stream.seekg(static_cast<std::streamoff>(nameTableOffset));
			if (!a_stream) {
				return std::nullopt;
			}
			std::uint64_t consumed = nameTableOffset;
			std::string path;
			for (std::uint32_t index = 0; index < report.fileCount; ++index) {
				std::uint16_t length = 0;
				if (!ReadLittle(a_stream, length) || length > kMaximumPathBytes || consumed + 2 + length > a_sizeBytes) {
					return std::nullopt;
				}
				consumed += 2 + length;
				path.resize(length);
				a_stream.read(path.data(), length);
				if (a_stream.gcount() != length) {
					return std::nullopt;
				}
				const auto lowered = Lower(path);
				if (lowered.starts_with("scripts\\") || lowered.ends_with(".pex")) {
					++report.scripts;
				}
				if (lowered.starts_with("interface\\") || lowered.ends_with(".swf")) {
					++report.interfaceFiles;
				}
				if (IsRefusedInnerPath(path)) {
					++report.refusedCount;
					if (report.refusedPaths.size() < kMaximumListedRefusals) {
						report.refusedPaths.push_back(path);
					}
				}
			}
			return report;
		} catch (...) {
			return std::nullopt;
		}
	}

	std::optional<Report> InspectFile(const std::filesystem::path& a_path)
	{
		std::error_code error;
		const auto size = std::filesystem::file_size(a_path, error);
		if (error) {
			return std::nullopt;
		}
		std::ifstream stream(a_path, std::ios::binary);
		if (!stream) {
			return std::nullopt;
		}
		return Inspect(stream, size);
	}

	bool IsAcceptable(const Report& a_report) noexcept
	{
		const bool knownVersion = a_report.version == 1 || a_report.version == 7 || a_report.version == 8;
		return knownVersion && a_report.refusedCount == 0;
	}
}
