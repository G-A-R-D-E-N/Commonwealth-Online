#include "F4TSaveFilePlugins.h"

#include <algorithm>
#include <cstring>
#include <format>
#include <unordered_set>

namespace
{
	constexpr std::uint32_t kMaximumImageDimension = 8192;
	constexpr std::size_t kFixedHeaderBytes = 16;

	class Reader
	{
	public:
		explicit Reader(std::span<const std::uint8_t> a_bytes) :
			_bytes(a_bytes)
		{}

		[[nodiscard]] bool ReadU8(std::uint8_t& a_value)
		{
			if (_position + sizeof(a_value) > _bytes.size()) {
				return false;
			}
			a_value = _bytes[_position];
			_position += sizeof(a_value);
			return true;
		}

		[[nodiscard]] bool ReadU16(std::uint16_t& a_value)
		{
			if (_position + sizeof(a_value) > _bytes.size()) {
				return false;
			}
			a_value = static_cast<std::uint16_t>(_bytes[_position]) |
			          static_cast<std::uint16_t>(_bytes[_position + 1] << 8);
			_position += sizeof(a_value);
			return true;
		}

		[[nodiscard]] bool ReadU32(std::uint32_t& a_value)
		{
			if (_position + sizeof(a_value) > _bytes.size()) {
				return false;
			}
			a_value = static_cast<std::uint32_t>(_bytes[_position]) |
			          (static_cast<std::uint32_t>(_bytes[_position + 1]) << 8) |
			          (static_cast<std::uint32_t>(_bytes[_position + 2]) << 16) |
			          (static_cast<std::uint32_t>(_bytes[_position + 3]) << 24);
			_position += sizeof(a_value);
			return true;
		}

		[[nodiscard]] bool Skip(std::uint64_t a_count)
		{
			if (a_count > Remaining()) {
				return false;
			}
			_position += static_cast<std::size_t>(a_count);
			return true;
		}

		[[nodiscard]] bool ReadFixed(std::string_view a_expected)
		{
			if (a_expected.size() > Remaining()) {
				return false;
			}
			if (std::memcmp(_bytes.data() + _position, a_expected.data(), a_expected.size()) != 0) {
				return false;
			}
			_position += a_expected.size();
			return true;
		}

		[[nodiscard]] bool ReadString(std::size_t a_maxLength, std::string& a_value)
		{
			std::uint16_t length{};
			if (!ReadU16(length)) {
				return false;
			}
			if (length > a_maxLength || length > Remaining()) {
				return false;
			}
			a_value.assign(reinterpret_cast<const char*>(_bytes.data() + _position), length);
			_position += length;
			return true;
		}

		[[nodiscard]] std::size_t Position() const { return _position; }
		[[nodiscard]] std::size_t Remaining() const { return _bytes.size() - _position; }

	private:
		std::span<const std::uint8_t> _bytes;
		std::size_t                   _position{};
	};

	bool ReadMetadata(Reader& a_reader, std::uint32_t& a_headerSize, std::uint32_t& a_version, std::uint32_t& a_width, std::uint32_t& a_height)
	{
		std::string text;
		std::uint32_t ignored{};
		std::uint16_t ignoredShort{};

		if (!a_reader.ReadFixed(F4T::SaveFilePlugins::kSaveMagic) ||
			!a_reader.ReadU32(a_headerSize) ||
			!a_reader.ReadU32(a_version) ||
			!a_reader.ReadU32(ignored) ||
			!a_reader.ReadString(F4T::SaveFilePlugins::kMaximumHeaderStringBytes, text) ||
			!a_reader.ReadU32(ignored) ||
			!a_reader.ReadString(F4T::SaveFilePlugins::kMaximumHeaderStringBytes, text) ||
			!a_reader.ReadString(F4T::SaveFilePlugins::kMaximumHeaderStringBytes, text) ||
			!a_reader.ReadString(F4T::SaveFilePlugins::kMaximumHeaderStringBytes, text) ||
			!a_reader.ReadU16(ignoredShort) ||
			!a_reader.Skip(4) ||
			!a_reader.Skip(4) ||
			!a_reader.Skip(8) ||
			!a_reader.ReadU32(a_width) ||
			!a_reader.ReadU32(a_height)) {
			return false;
		}

		if (a_headerSize != a_reader.Position() - kFixedHeaderBytes) {
			return false;
		}
		return true;
	}

	bool IsPlausiblePluginName(std::string_view a_name)
	{
		if (a_name.empty() || a_name.size() > F4T::SaveFilePlugins::kMaximumPluginNameBytes) {
			return false;
		}
		if (a_name.find('.') == std::string_view::npos) {
			return false;
		}
		for (const char character : a_name) {
			const auto value = static_cast<unsigned char>(character);
			if (value < 0x20 || value == 0x7F) {
				return false;
			}
			if (character == '/' || character == '\\' || character == ':') {
				return false;
			}
		}
		return true;
	}

	bool ReadPluginName(Reader& a_reader, std::string& a_name)
	{
		if (!a_reader.ReadString(F4T::SaveFilePlugins::kMaximumPluginNameBytes, a_name)) {
			return false;
		}
		return IsPlausiblePluginName(a_name);
	}

	bool ReadPluginNames(Reader& a_reader, std::size_t a_count, std::vector<std::string>& a_names)
	{
		a_names.clear();
		a_names.reserve(a_count);
		for (std::size_t index = 0; index < a_count; ++index) {
			std::string name;
			if (!ReadPluginName(a_reader, name)) {
				return false;
			}
			a_names.push_back(std::move(name));
		}
		return true;
	}

	std::string ToLowerAscii(std::string_view a_text)
	{
		std::string result{ a_text };
		for (auto& character : result) {
			if (character >= 'A' && character <= 'Z') {
				character = static_cast<char>(character - 'A' + 'a');
			}
		}
		return result;
	}
}

namespace F4T::SaveFilePlugins
{
	std::optional<PluginList> ParseSavePlugins(std::span<const std::uint8_t> a_headerBytes)
	{
		Reader reader{ a_headerBytes };
		std::uint32_t headerSize{};
		std::uint32_t version{};
		std::uint32_t width{};
		std::uint32_t height{};
		if (!ReadMetadata(reader, headerSize, version, width, height)) {
			return std::nullopt;
		}
		if (version < kMinimumSaveVersion || version > kMaximumSaveVersion) {
			return std::nullopt;
		}
		if (width == 0 || height == 0 || width > kMaximumImageDimension || height > kMaximumImageDimension) {
			return std::nullopt;
		}
		if (!reader.Skip(4ull * static_cast<std::uint64_t>(width) * static_cast<std::uint64_t>(height))) {
			return std::nullopt;
		}

		std::uint8_t formVersion{};
		std::string gameVersion;
		std::uint32_t pluginInfoSize{};
		if (!reader.ReadU8(formVersion) ||
			!reader.ReadString(kMaximumHeaderStringBytes, gameVersion) ||
			!reader.ReadU32(pluginInfoSize)) {
			return std::nullopt;
		}
		if (formVersion == 0 || gameVersion.empty()) {
			return std::nullopt;
		}
		if (pluginInfoSize == 0 || pluginInfoSize > reader.Remaining()) {
			return std::nullopt;
		}

		const auto blockStart = reader.Position();
		std::uint8_t fullCount{};
		PluginList result;
		if (!reader.ReadU8(fullCount) || !ReadPluginNames(reader, fullCount, result.full)) {
			return std::nullopt;
		}
		const auto fullBlockBytes = reader.Position() - blockStart;

		if (fullBlockBytes == pluginInfoSize) {
			return result;
		}

		std::uint16_t lightCount{};
		if (!reader.ReadU16(lightCount) || !ReadPluginNames(reader, lightCount, result.light)) {
			return std::nullopt;
		}
		if (reader.Position() - blockStart != pluginInfoSize) {
			return std::nullopt;
		}
		return result;
	}

	std::vector<std::string> FindMissingPlugins(const PluginList& a_saved, const PluginList& a_loaded)
	{
		std::unordered_set<std::string> loaded;
		loaded.reserve(a_loaded.full.size() + a_loaded.light.size());
		for (const auto& name : a_loaded.full) {
			loaded.insert(ToLowerAscii(name));
		}
		for (const auto& name : a_loaded.light) {
			loaded.insert(ToLowerAscii(name));
		}

		std::vector<std::string> missing;
		const auto collect = [&](const std::vector<std::string>& a_names) {
			for (const auto& name : a_names) {
				if (!loaded.contains(ToLowerAscii(name))) {
					missing.push_back(name);
				}
			}
		};
		collect(a_saved.full);
		collect(a_saved.light);
		return missing;
	}

	std::string BuildMissingPluginsMessage(const std::vector<std::string>& a_missing)
	{
		const std::size_t shown = std::min(a_missing.size(), kMaximumNamedPlugins);
		std::string names;
		for (std::size_t index = 0; index < shown; ++index) {
			if (index > 0) {
				names += ", ";
			}
			names += a_missing[index];
		}
		if (a_missing.size() > shown) {
			names += std::format(" and {} more", a_missing.size() - shown);
		}
		return std::format(
			"This save needs plugins that are not loaded: {}. Install the server modpack or pick another save.",
			names);
	}
}
