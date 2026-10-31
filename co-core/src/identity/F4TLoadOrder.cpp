#include "F4TLoadOrder.h"

#include "F4TFormIdentity.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <charconv>
#include <unordered_set>

namespace
{
	using F4T::LoadOrder::Plugin;
	using F4T::LoadOrder::PluginKind;
	using F4T::LoadOrder::PluginList;

	constexpr char kBase64Alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
	constexpr std::size_t kMaximumBase64Chars = ((F4T::LoadOrder::kMaximumPayloadBytes + 2) / 3) * 4;

	std::string AsciiLower(std::string_view a_text)
	{
		std::string lower{ a_text };
		for (auto& character : lower) {
			if (character >= 'A' && character <= 'Z') {
				character = static_cast<char>(character - 'A' + 'a');
			}
		}
		return lower;
	}

	bool IsValidPluginName(std::string_view a_name) noexcept
	{
		if (a_name.empty() || a_name.size() > F4T::LoadOrder::kMaximumPluginNameBytes) {
			return false;
		}
		for (const auto character : a_name) {
			const auto value = static_cast<unsigned char>(character);
			if (value < 0x20 || value == 0x7F || value == '/' || value == '\\') {
				return false;
			}
		}
		return true;
	}

	std::string Base64Encode(std::string_view a_bytes)
	{
		std::string encoded;
		encoded.reserve(((a_bytes.size() + 2) / 3) * 4);
		std::size_t index = 0;
		while (index + 2 < a_bytes.size()) {
			const auto block = (static_cast<std::uint32_t>(static_cast<unsigned char>(a_bytes[index])) << 16) |
				(static_cast<std::uint32_t>(static_cast<unsigned char>(a_bytes[index + 1])) << 8) |
				static_cast<std::uint32_t>(static_cast<unsigned char>(a_bytes[index + 2]));
			encoded.push_back(kBase64Alphabet[(block >> 18) & 0x3F]);
			encoded.push_back(kBase64Alphabet[(block >> 12) & 0x3F]);
			encoded.push_back(kBase64Alphabet[(block >> 6) & 0x3F]);
			encoded.push_back(kBase64Alphabet[block & 0x3F]);
			index += 3;
		}
		const auto remaining = a_bytes.size() - index;
		if (remaining == 1) {
			const auto block = static_cast<std::uint32_t>(static_cast<unsigned char>(a_bytes[index])) << 16;
			encoded.push_back(kBase64Alphabet[(block >> 18) & 0x3F]);
			encoded.push_back(kBase64Alphabet[(block >> 12) & 0x3F]);
			encoded.append("==");
		} else if (remaining == 2) {
			const auto block = (static_cast<std::uint32_t>(static_cast<unsigned char>(a_bytes[index])) << 16) |
				(static_cast<std::uint32_t>(static_cast<unsigned char>(a_bytes[index + 1])) << 8);
			encoded.push_back(kBase64Alphabet[(block >> 18) & 0x3F]);
			encoded.push_back(kBase64Alphabet[(block >> 12) & 0x3F]);
			encoded.push_back(kBase64Alphabet[(block >> 6) & 0x3F]);
			encoded.push_back('=');
		}
		return encoded;
	}

	std::optional<std::string> Base64Decode(std::string_view a_text)
	{
		if (a_text.size() % 4 != 0) {
			return std::nullopt;
		}
		std::array<std::int16_t, 256> values{};
		values.fill(-1);
		for (std::size_t i = 0; i < 64; ++i) {
			values[static_cast<unsigned char>(kBase64Alphabet[i])] = static_cast<std::int16_t>(i);
		}

		std::string decoded;
		decoded.reserve((a_text.size() / 4) * 3);
		for (std::size_t offset = 0; offset < a_text.size(); offset += 4) {
			std::uint32_t block = 0;
			std::size_t padding = 0;
			for (std::size_t i = 0; i < 4; ++i) {
				const auto character = static_cast<unsigned char>(a_text[offset + i]);
				if (character == '=') {
					if (offset + 4 != a_text.size() || i < 2) {
						return std::nullopt;
					}
					++padding;
					block <<= 6;
					continue;
				}
				if (padding != 0) {
					return std::nullopt;
				}
				const auto value = values[character];
				if (value < 0) {
					return std::nullopt;
				}
				block = (block << 6) | static_cast<std::uint32_t>(value);
			}
			decoded.push_back(static_cast<char>((block >> 16) & 0xFF));
			if (padding < 2) {
				decoded.push_back(static_cast<char>((block >> 8) & 0xFF));
			}
			if (padding < 1) {
				decoded.push_back(static_cast<char>(block & 0xFF));
			}
		}
		return decoded;
	}

	void AppendU16(std::string& a_bytes, std::uint16_t a_value)
	{
		a_bytes.push_back(static_cast<char>(a_value & 0xFF));
		a_bytes.push_back(static_cast<char>((a_value >> 8) & 0xFF));
	}

	std::optional<std::uint16_t> ReadU16(const char*& a_cursor, const char* a_end)
	{
		if (a_end - a_cursor < 2) {
			return std::nullopt;
		}
		const auto value = static_cast<std::uint16_t>(static_cast<unsigned char>(a_cursor[0])) |
			(static_cast<std::uint16_t>(static_cast<unsigned char>(a_cursor[1])) << 8);
		a_cursor += 2;
		return value;
	}

	std::string JoinNames(const std::vector<std::string>& a_names)
	{
		std::string joined;
		const auto shown = (std::min)(a_names.size(), F4T::LoadOrder::kMaximumDifferenceNames);
		for (std::size_t i = 0; i < shown; ++i) {
			if (i != 0) {
				joined += ", ";
			}
			const auto name = std::string_view{ a_names[i] }
			                      .substr(0, F4T::LoadOrder::kMaximumDifferenceNameBytes);
			joined += name;
		}
		if (a_names.size() > shown) {
			joined += " (+" + std::to_string(a_names.size() - shown) + " more)";
		}
		return joined;
	}
}

namespace F4T::LoadOrder
{
	bool IsValidPluginList(const PluginList& a_list) noexcept
	{
		try {
			if (a_list.full.empty() ||
				a_list.full.size() > kMaximumFullPlugins ||
				a_list.light.size() > kMaximumLightPlugins) {
				return false;
			}
			std::unordered_set<std::string> names;
			std::unordered_set<std::uint16_t> fullSlots;
			std::unordered_set<std::uint16_t> lightSlots;
			for (const auto& plugin : a_list.full) {
				if (plugin.kind != PluginKind::full ||
					plugin.index >= static_cast<std::uint16_t>(FormIdentity::LightFileMarker) ||
					!IsValidPluginName(plugin.name) ||
					!fullSlots.insert(plugin.index).second ||
					!names.insert(AsciiLower(plugin.name)).second) {
					return false;
				}
			}
			for (const auto& plugin : a_list.light) {
				if (plugin.kind != PluginKind::light ||
					plugin.index > FormIdentity::MaximumLightFileIndex ||
					!IsValidPluginName(plugin.name) ||
					!lightSlots.insert(plugin.index).second ||
					!names.insert(AsciiLower(plugin.name)).second) {
					return false;
				}
			}
			return true;
		} catch (...) {
			return false;
		}
	}

	std::optional<std::string> EncodePluginList(const PluginList& a_list)
	{
		if (!IsValidPluginList(a_list)) {
			return std::nullopt;
		}
		try {
			std::string bytes;
			bytes.reserve(a_list.full.size() * 32 + a_list.light.size() * 32 + 5);
			bytes.push_back(static_cast<char>(kFormatVersion));
			AppendU16(bytes, static_cast<std::uint16_t>(a_list.full.size()));
			AppendU16(bytes, static_cast<std::uint16_t>(a_list.light.size()));
			for (const auto& plugin : a_list.full) {
				bytes.push_back(static_cast<char>(plugin.index));
				bytes.push_back(static_cast<char>(plugin.name.size()));
				bytes += plugin.name;
			}
			for (const auto& plugin : a_list.light) {
				AppendU16(bytes, plugin.index);
				bytes.push_back(static_cast<char>(plugin.name.size()));
				bytes += plugin.name;
			}
			if (bytes.size() > kMaximumPayloadBytes) {
				return std::nullopt;
			}
			return Base64Encode(bytes);
		} catch (...) {
			return std::nullopt;
		}
	}

	std::optional<PluginList> DecodePluginList(std::string_view a_base64)
	{
		if (a_base64.empty() || a_base64.size() > kMaximumBase64Chars) {
			return std::nullopt;
		}
		try {
			const auto decoded = Base64Decode(a_base64);
			if (!decoded) {
				return std::nullopt;
			}
			const auto* cursor = decoded->data();
			const auto* end = cursor + decoded->size();
			if (decoded->size() < 5 || static_cast<std::uint8_t>(*cursor++) != kFormatVersion) {
				return std::nullopt;
			}
			const auto fullCount = ReadU16(cursor, end);
			const auto lightCount = ReadU16(cursor, end);
			if (!fullCount || !lightCount ||
				*fullCount == 0 || *fullCount > kMaximumFullPlugins ||
				*lightCount > kMaximumLightPlugins) {
				return std::nullopt;
			}
			auto readName = [&](std::uint8_t a_length) -> std::optional<std::string> {
				if (a_length == 0 || end - cursor < a_length) {
					return std::nullopt;
				}
				std::string name{ cursor, static_cast<std::size_t>(a_length) };
				cursor += a_length;
				return name;
			};

			PluginList list;
			list.full.reserve(*fullCount);
			list.light.reserve(*lightCount);
			for (std::uint16_t i = 0; i < *fullCount; ++i) {
				if (end - cursor < 2) {
					return std::nullopt;
				}
				const auto index = static_cast<std::uint8_t>(*cursor++);
				const auto length = static_cast<std::uint8_t>(*cursor++);
				auto name = readName(length);
				if (!name) {
					return std::nullopt;
				}
				list.full.push_back(Plugin{ PluginKind::full, index, std::move(*name) });
			}
			for (std::uint16_t i = 0; i < *lightCount; ++i) {
				const auto index = ReadU16(cursor, end);
				if (!index || end - cursor < 1) {
					return std::nullopt;
				}
				const auto length = static_cast<std::uint8_t>(*cursor++);
				auto name = readName(length);
				if (!name) {
					return std::nullopt;
				}
				list.light.push_back(Plugin{ PluginKind::light, *index, std::move(*name) });
			}
			if (cursor != end || !IsValidPluginList(list)) {
				return std::nullopt;
			}
			return list;
		} catch (...) {
			return std::nullopt;
		}
	}

	std::optional<std::string> BuildPluginListPacket(const PluginList& a_list)
	{
		const auto data = EncodePluginList(a_list);
		if (!data) {
			return std::nullopt;
		}
		try {
			nlohmann::json packet{
				{ "type", std::string{ kPacketType } },
				{ "data", *data }
			};
			return packet.dump();
		} catch (...) {
			return std::nullopt;
		}
	}

	std::uint32_t TranslationTable::ToLocal(std::uint32_t a_formId) const noexcept
	{
		const auto topByte = static_cast<std::uint8_t>(a_formId >> 24);
		if (a_formId == 0 || topByte == FormIdentity::RuntimeFileMarker) {
			return a_formId;
		}
		if (topByte == FormIdentity::LightFileMarker) {
			const auto slot = (a_formId >> 12) & FormIdentity::MaximumLightFileIndex;
			if (slot >= light.size() || light[slot] < 0) {
				return 0;
			}
			return (static_cast<std::uint32_t>(FormIdentity::LightFileMarker) << 24) |
				(static_cast<std::uint32_t>(light[slot]) << 12) |
				(a_formId & FormIdentity::MaximumLightLocalId);
		}
		if (full[topByte] < 0) {
			return 0;
		}
		return (static_cast<std::uint32_t>(full[topByte]) << 24) | (a_formId & FormIdentity::MaximumRegularLocalId);
	}

	TranslationTable BuildTranslationTable(const PluginList& a_local, const PluginList& a_remote)
	{
		TranslationTable table;
		table.full = MissingFullSlots();
		table.light.assign(static_cast<std::size_t>(FormIdentity::MaximumLightFileIndex) + 1, -1);

		std::unordered_map<std::string, std::uint16_t> localFull;
		std::unordered_map<std::string, std::uint16_t> localLight;
		for (const auto& plugin : a_local.full) {
			localFull.emplace(AsciiLower(plugin.name), plugin.index);
		}
		for (const auto& plugin : a_local.light) {
			localLight.emplace(AsciiLower(plugin.name), plugin.index);
		}

		table.identical = a_local.full.size() == a_remote.full.size() &&
			a_local.light.size() == a_remote.light.size();

		for (const auto& plugin : a_remote.full) {
			const auto key = AsciiLower(plugin.name);
			const auto found = localFull.find(key);
			if (found == localFull.end()) {
				table.identical = false;
				table.missingLocal.push_back(plugin.name);
				continue;
			}
			if (plugin.index < table.full.size()) {
				table.full[plugin.index] = static_cast<std::int16_t>(found->second);
			}
			if (found->second != plugin.index) {
				table.identical = false;
				table.reordered = true;
			}
		}
		for (const auto& plugin : a_remote.light) {
			const auto key = AsciiLower(plugin.name);
			const auto found = localLight.find(key);
			if (found == localLight.end()) {
				table.identical = false;
				table.missingLocal.push_back(plugin.name);
				continue;
			}
			if (plugin.index < table.light.size()) {
				table.light[plugin.index] = static_cast<std::int16_t>(found->second);
			}
			if (found->second != plugin.index) {
				table.identical = false;
				table.reordered = true;
			}
		}

		std::unordered_set<std::string> remoteNames;
		for (const auto& plugin : a_remote.full) {
			remoteNames.insert(AsciiLower(plugin.name));
		}
		for (const auto& plugin : a_remote.light) {
			remoteNames.insert(AsciiLower(plugin.name));
		}
		for (const auto& plugin : a_local.full) {
			if (!remoteNames.contains(AsciiLower(plugin.name))) {
				table.missingRemote.push_back(plugin.name);
			}
		}
		for (const auto& plugin : a_local.light) {
			if (!remoteNames.contains(AsciiLower(plugin.name))) {
				table.missingRemote.push_back(plugin.name);
			}
		}

		try {
			std::string differences;
			if (!table.missingLocal.empty()) {
				differences += "They have mods you don't: " + JoinNames(table.missingLocal) + ". ";
			}
			if (!table.missingRemote.empty()) {
				differences += "You have mods they don't: " + JoinNames(table.missingRemote) + ". ";
			}
			if (differences.empty() && table.reordered) {
				differences = "Same mods, different load order (translated).";
			}
			if (!differences.empty() && differences.back() == ' ') {
				differences.pop_back();
			}
			table.differences = std::move(differences);
		} catch (...) {
			table.differences.clear();
		}
		return table;
	}

	void Registry::SetLocalList(PluginList a_list)
	{
		if (!IsValidPluginList(a_list)) {
			return;
		}
		const std::scoped_lock lock(_mutex);
		_local = std::move(a_list);
		RebuildLocked();
	}

	void Registry::SetRemoteList(std::uint32_t a_playerId, PluginList a_list)
	{
		if (a_playerId == 0 || !IsValidPluginList(a_list)) {
			return;
		}
		const std::scoped_lock lock(_mutex);
		_remoteLists.insert_or_assign(a_playerId, std::move(a_list));
		if (_local) {
			_tables.insert_or_assign(a_playerId, BuildTranslationTable(*_local, _remoteLists.at(a_playerId)));
		}
	}

	void Registry::RebuildLocked()
	{
		_tables.clear();
		if (!_local) {
			return;
		}
		for (const auto& [playerId, remote] : _remoteLists) {
			_tables.insert_or_assign(playerId, BuildTranslationTable(*_local, remote));
		}
	}

	bool Registry::HasLocalList() const
	{
		const std::scoped_lock lock(_mutex);
		return _local.has_value();
	}

	bool Registry::Known(std::uint32_t a_playerId) const
	{
		const std::scoped_lock lock(_mutex);
		return _tables.contains(a_playerId);
	}

	bool Registry::Identical(std::uint32_t a_playerId) const
	{
		const std::scoped_lock lock(_mutex);
		const auto found = _tables.find(a_playerId);
		return found != _tables.end() && found->second.identical;
	}

	std::uint32_t Registry::ToLocal(std::uint32_t a_playerId, std::uint32_t a_formId) const
	{
		const auto topByte = static_cast<std::uint8_t>(a_formId >> 24);
		if (a_formId == 0 || topByte == FormIdentity::RuntimeFileMarker) {
			return a_formId;
		}
		const std::scoped_lock lock(_mutex);
		const auto found = _tables.find(a_playerId);
		if (found == _tables.end() || found->second.identical) {
			return a_formId;
		}
		const auto local = found->second.ToLocal(a_formId);
		if (local == 0) {
			_dropped.fetch_add(1, std::memory_order_relaxed);
		}
		return local;
	}

	std::string Registry::Differences(std::uint32_t a_playerId) const
	{
		const std::scoped_lock lock(_mutex);
		const auto found = _tables.find(a_playerId);
		return found == _tables.end() ? std::string{} : found->second.differences;
	}

	std::uint64_t Registry::Dropped() const noexcept
	{
		return _dropped.load(std::memory_order_relaxed);
	}

	void Registry::Forget(std::uint32_t a_playerId)
	{
		const std::scoped_lock lock(_mutex);
		_remoteLists.erase(a_playerId);
		_tables.erase(a_playerId);
	}

	void Registry::Reset()
	{
		const std::scoped_lock lock(_mutex);
		_local.reset();
		_remoteLists.clear();
		_tables.clear();
	}
}
