#include "F4TModpackManifest.h"

#include "F4TContentManifest.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>

namespace F4T::ModpackManifest
{
	namespace
	{
		using Json = nlohmann::json;
		using EntryKey = std::pair<EntryKind, std::string>;

		constexpr std::size_t kMaximumEntries = 8192;
		constexpr std::size_t kMaximumParseDepth = 8;
		constexpr std::size_t kMaximumPluginOrders = 4351;
		constexpr std::size_t kMaximumNameLength = 260;
		constexpr std::size_t kMaximumMismatchTextBytes = 2048;
		constexpr std::size_t kMaximumNamesPerSection = 8;
		constexpr std::size_t kMismatchReserveBytes = 320;
		constexpr std::uint64_t kSchemaVersion = 1;
		constexpr std::string_view kMismatchHeader = "MODPACK MISMATCH\n";
		constexpr std::string_view kMismatchFallback = "Your mods do not match this server.";
		constexpr std::string_view kFingerprintPrefix = "co-modpack-v1\n";
		constexpr std::string_view kForbiddenNameCharacters = "<>\":|?*";

		constexpr std::array<std::string_view, 4> kKindNames{ "plugin", "native", "config", "archive" };
		constexpr std::array<std::string_view, 4> kPluginTypeNames{ "none", "esm", "esp", "esl" };
		constexpr std::array<std::string_view, 7> kSectionTitles{
			"Missing:",
			"Unexpected:",
			"Wrong version/content:",
			"Wrong native plugin:",
			"Wrong config:",
			"Wrong archive:",
			"Wrong load order:"
		};

		template <typename Enum, std::size_t Count>
		std::optional<Enum> FindEnum(const std::array<std::string_view, Count>& a_names, std::string_view a_value)
		{
			for (std::size_t index = 0; index < Count; ++index) {
				if (a_names[index] == a_value) {
					return static_cast<Enum>(index);
				}
			}
			return std::nullopt;
		}

		bool HasOnlyKeys(const Json& a_object, std::initializer_list<std::string_view> a_keys)
		{
			for (const auto& item : a_object.items()) {
				if (std::find(a_keys.begin(), a_keys.end(), std::string_view{ item.key() }) == a_keys.end()) {
					return false;
				}
			}
			return true;
		}

		bool IsValidUtf8(std::string_view a_bytes) noexcept
		{
			std::size_t index = 0;
			while (index < a_bytes.size()) {
				const auto lead = static_cast<unsigned char>(a_bytes[index]);
				std::size_t length = 1;
				std::uint32_t codePoint = lead;
				if (lead >= 0xc2 && lead <= 0xdf) {
					length = 2;
					codePoint = lead & 0x1f;
				} else if (lead >= 0xe0 && lead <= 0xef) {
					length = 3;
					codePoint = lead & 0x0f;
				} else if (lead >= 0xf0 && lead <= 0xf4) {
					length = 4;
					codePoint = lead & 0x07;
				} else if (lead >= 0x80) {
					return false;
				}
				if (index + length > a_bytes.size()) {
					return false;
				}
				for (std::size_t offset = 1; offset < length; ++offset) {
					const auto continuation = static_cast<unsigned char>(a_bytes[index + offset]);
					if ((continuation & 0xc0) != 0x80) {
						return false;
					}
					codePoint = (codePoint << 6) | (continuation & 0x3f);
				}
				if ((length == 3 && (codePoint < 0x800 || (codePoint >= 0xd800 && codePoint <= 0xdfff))) ||
					(length == 4 && (codePoint < 0x10000 || codePoint > 0x10ffff))) {
					return false;
				}
				index += length;
			}
			return true;
		}

		bool HasBidirectionalControl(std::string_view a_name) noexcept
		{
			for (std::size_t index = 0; index < a_name.size(); ++index) {
				const auto lead = static_cast<unsigned char>(a_name[index]);
				if (lead == 0xd8 && index + 1 < a_name.size() && static_cast<unsigned char>(a_name[index + 1]) == 0x9c) {
					return true;
				}
				if (lead != 0xe2 || index + 2 >= a_name.size()) {
					continue;
				}
				const auto second = static_cast<unsigned char>(a_name[index + 1]);
				const auto third = static_cast<unsigned char>(a_name[index + 2]);
				if ((second == 0x80 && (third == 0x8e || third == 0x8f || (third >= 0xaa && third <= 0xae))) ||
					(second == 0x81 && third >= 0xa6 && third <= 0xa9)) {
					return true;
				}
			}
			return false;
		}

		bool IsDeviceSegment(std::string_view a_segment) noexcept
		{
			auto stem = a_segment.substr(0, a_segment.find('.'));
			while (!stem.empty() && stem.back() == ' ') {
				stem.remove_suffix(1);
			}
			if (stem == "con" || stem == "prn" || stem == "aux" || stem == "nul" || stem == "conin$" || stem == "conout$") {
				return true;
			}
			if (stem.size() < 4 || !(stem.starts_with("com") || stem.starts_with("lpt"))) {
				return false;
			}
			const auto suffix = stem.substr(3);
			return suffix == "\xc2\xb9" || suffix == "\xc2\xb2" || suffix == "\xc2\xb3" ||
				(suffix.size() == 1 && suffix[0] >= '0' && suffix[0] <= '9');
		}

		std::optional<std::string> NormalizeName(std::string a_name, EntryKind a_kind)
		{
			if (a_name.empty() || a_name.size() > kMaximumNameLength || !IsValidUtf8(a_name) || HasBidirectionalControl(a_name)) {
				return std::nullopt;
			}
			for (auto& ch : a_name) {
				const auto byte = static_cast<unsigned char>(ch);
				if (byte < 0x20 || byte == 0x7f || kForbiddenNameCharacters.find(ch) != std::string_view::npos) {
					return std::nullopt;
				}
				if (ch == '\\') {
					if (a_kind == EntryKind::plugin) {
						return std::nullopt;
					}
					ch = '/';
				} else if (ch >= 'A' && ch <= 'Z') {
					ch = static_cast<char>(ch - 'A' + 'a');
				}
			}
			if (a_kind == EntryKind::plugin && a_name.contains('/')) {
				return std::nullopt;
			}
			std::size_t cursor = 0;
			while (true) {
				const auto next = a_name.find('/', cursor);
				const auto segment = std::string_view{ a_name }.substr(
					cursor,
					next == std::string::npos ? std::string::npos : next - cursor);
				if (segment.empty() || segment == "." || segment == ".." || segment.back() == '.' || segment.back() == ' ' ||
					IsDeviceSegment(segment)) {
					return std::nullopt;
				}
				if (next == std::string::npos) {
					break;
				}
				cursor = next + 1;
			}
			return a_name;
		}

		std::optional<Entry> NormalizeEntry(Entry a_entry)
		{
			auto name = NormalizeName(std::move(a_entry.name), a_entry.kind);
			if (!name) {
				return std::nullopt;
			}
			a_entry.name = std::move(*name);
			const bool hasSha = !a_entry.sha256Hex.empty();
			const bool hasSize = a_entry.sizeBytes.has_value();
			if (a_entry.kind == EntryKind::plugin) {
				if (a_entry.pluginType == PluginType::none || a_entry.order >= kMaximumPluginOrders || hasSha == hasSize) {
					return std::nullopt;
				}
			} else if (a_entry.kind == EntryKind::archive) {
				if (a_entry.pluginType != PluginType::none || a_entry.order != 0 || !hasSha || !hasSize) {
					return std::nullopt;
				}
			} else if (a_entry.pluginType != PluginType::none || a_entry.order != 0 || hasSize || !hasSha) {
				return std::nullopt;
			}
			if (hasSha && !ContentManifest::IsValidFingerprint(a_entry.sha256Hex)) {
				return std::nullopt;
			}
			return a_entry;
		}

		std::optional<Entry> ReadEntry(const Json& a_object)
		{
			if (!a_object.is_object()) {
				return std::nullopt;
			}
			const auto kindIt = a_object.find("kind");
			if (kindIt == a_object.end() || !kindIt->is_string()) {
				return std::nullopt;
			}
			const auto kind = FindEnum<EntryKind>(kKindNames, kindIt->get<std::string>());
			if (!kind) {
				return std::nullopt;
			}
			const bool plugin = *kind == EntryKind::plugin;
			const bool keysAllowed = plugin
				? HasOnlyKeys(a_object, { "kind", "name", "pluginType", "order", "sha256", "size" })
				: *kind == EntryKind::archive
					? HasOnlyKeys(a_object, { "kind", "name", "sha256", "size" })
					: HasOnlyKeys(a_object, { "kind", "name", "sha256" });
			if (!keysAllowed) {
				return std::nullopt;
			}
			const auto nameIt = a_object.find("name");
			if (nameIt == a_object.end() || !nameIt->is_string()) {
				return std::nullopt;
			}
			Entry entry;
			entry.kind = *kind;
			entry.name = nameIt->get<std::string>();
			if (plugin) {
				const auto typeIt = a_object.find("pluginType");
				const auto orderIt = a_object.find("order");
				if (typeIt == a_object.end() || !typeIt->is_string() || orderIt == a_object.end() || !orderIt->is_number_unsigned()) {
					return std::nullopt;
				}
				const auto pluginType = FindEnum<PluginType>(kPluginTypeNames, typeIt->get<std::string>());
				const auto order = orderIt->get<std::uint64_t>();
				if (!pluginType || order >= kMaximumPluginOrders) {
					return std::nullopt;
				}
				entry.pluginType = *pluginType;
				entry.order = static_cast<std::uint32_t>(order);
			}
			if (const auto shaIt = a_object.find("sha256"); shaIt != a_object.end()) {
				if (!shaIt->is_string()) {
					return std::nullopt;
				}
				entry.sha256Hex = shaIt->get<std::string>();
				if (entry.sha256Hex.empty()) {
					return std::nullopt;
				}
			}
			if (const auto sizeIt = a_object.find("size"); sizeIt != a_object.end()) {
				if (!sizeIt->is_number_unsigned()) {
					return std::nullopt;
				}
				entry.sizeBytes = sizeIt->get<std::uint64_t>();
			}
			return entry;
		}

		class StructureScan final : public Json::json_sax_t
		{
		public:
			bool null() override
			{
				return CountElement();
			}

			bool boolean(bool) override
			{
				return CountElement();
			}

			bool number_integer(Json::number_integer_t) override
			{
				return CountElement();
			}

			bool number_unsigned(Json::number_unsigned_t) override
			{
				return CountElement();
			}

			bool number_float(Json::number_float_t, const std::string&) override
			{
				return CountElement();
			}

			bool string(std::string&) override
			{
				return CountElement();
			}

			bool binary(Json::binary_t&) override
			{
				return false;
			}

			bool start_object(std::size_t) override
			{
				if (!CountElement() || frames_.size() >= kMaximumParseDepth) {
					return false;
				}
				frames_.push_back(Frame{ false, {}, 0 });
				return true;
			}

			bool key(std::string& a_key) override
			{
				return !frames_.empty() && !frames_.back().array && frames_.back().keys.insert(a_key).second;
			}

			bool end_object() override
			{
				frames_.pop_back();
				return true;
			}

			bool start_array(std::size_t) override
			{
				if (!CountElement() || frames_.size() >= kMaximumParseDepth) {
					return false;
				}
				frames_.push_back(Frame{ true, {}, 0 });
				return true;
			}

			bool end_array() override
			{
				frames_.pop_back();
				return true;
			}

			bool parse_error(std::size_t, const std::string&, const nlohmann::detail::exception&) override
			{
				return false;
			}

		private:
			struct Frame
			{
				bool array;
				std::set<std::string> keys;
				std::size_t elements;
			};

			bool CountElement()
			{
				return frames_.empty() || !frames_.back().array || ++frames_.back().elements <= kMaximumEntries;
			}

			std::vector<Frame> frames_;
		};

		bool SameContent(const Entry& a_required, const Entry& a_client)
		{
			return a_required.pluginType == a_client.pluginType && a_required.sha256Hex == a_client.sha256Hex &&
				a_required.sizeBytes == a_client.sizeBytes;
		}
	}

	bool IsValidModpackName(std::string_view a_name)
	{
		if (a_name.empty() || a_name.size() > kMaximumModpackNameBytes) {
			return false;
		}
		for (const unsigned char ch : a_name) {
			if (ch < 0x20 || ch > 0x7e || ch == '<' || ch == '>') {
				return false;
			}
		}
		return true;
	}

	std::optional<Manifest> NormalizeManifest(Manifest a_manifest)
	{
		try {
			if (a_manifest.entries.empty() || a_manifest.entries.size() > kMaximumEntries) {
				return std::nullopt;
			}
			Manifest normalized;
			normalized.entries.reserve(a_manifest.entries.size());
			std::set<EntryKey> keys;
			std::vector<std::uint32_t> pluginOrders;
			for (auto& entry : a_manifest.entries) {
				auto checked = NormalizeEntry(std::move(entry));
				if (!checked || !keys.insert(EntryKey{ checked->kind, checked->name }).second) {
					return std::nullopt;
				}
				if (checked->kind == EntryKind::plugin) {
					pluginOrders.push_back(checked->order);
				}
				normalized.entries.push_back(std::move(*checked));
			}
			std::sort(pluginOrders.begin(), pluginOrders.end());
			for (std::size_t index = 0; index < pluginOrders.size(); ++index) {
				if (pluginOrders[index] != index) {
					return std::nullopt;
				}
			}
			std::sort(normalized.entries.begin(), normalized.entries.end(), [](const Entry& a_lhs, const Entry& a_rhs) {
				return std::tie(a_lhs.kind, a_lhs.order, a_lhs.name) < std::tie(a_rhs.kind, a_rhs.order, a_rhs.name);
			});
			return normalized;
		} catch (...) {
			return std::nullopt;
		}
	}

	std::optional<Manifest> ParseManifest(std::string_view a_json)
	{
		try {
			if (a_json.size() > kMaximumManifestBytes) {
				return std::nullopt;
			}
			StructureScan scan;
			if (!Json::sax_parse(a_json, &scan)) {
				return std::nullopt;
			}
			const auto root = Json::parse(a_json, nullptr, false);
			if (root.is_discarded() || !root.is_object() || !HasOnlyKeys(root, { "version", "entries" })) {
				return std::nullopt;
			}
			const auto versionIt = root.find("version");
			if (versionIt == root.end() || !versionIt->is_number_unsigned() || versionIt->get<std::uint64_t>() != kSchemaVersion) {
				return std::nullopt;
			}
			const auto entriesIt = root.find("entries");
			if (entriesIt == root.end() || !entriesIt->is_array() || entriesIt->size() > kMaximumEntries) {
				return std::nullopt;
			}
			Manifest manifest;
			manifest.entries.reserve(entriesIt->size());
			for (const auto& item : *entriesIt) {
				auto entry = ReadEntry(item);
				if (!entry) {
					return std::nullopt;
				}
				manifest.entries.push_back(std::move(*entry));
			}
			return NormalizeManifest(std::move(manifest));
		} catch (...) {
			return std::nullopt;
		}
	}

	std::optional<std::string> SerializeManifest(const Manifest& a_manifest)
	{
		const auto normalized = NormalizeManifest(a_manifest);
		if (!normalized) {
			return std::nullopt;
		}
		try {
			Json entries = Json::array();
			for (const auto& entry : normalized->entries) {
				Json item{ { "kind", std::string{ kKindNames[static_cast<std::size_t>(entry.kind)] } }, { "name", entry.name } };
				if (entry.kind == EntryKind::plugin) {
					item["pluginType"] = std::string{ kPluginTypeNames[static_cast<std::size_t>(entry.pluginType)] };
					item["order"] = entry.order;
				}
				if (!entry.sha256Hex.empty()) {
					item["sha256"] = entry.sha256Hex;
				}
				if (entry.sizeBytes) {
					item["size"] = *entry.sizeBytes;
				}
				entries.push_back(std::move(item));
			}
			auto text = Json{ { "version", kSchemaVersion }, { "entries", std::move(entries) } }.dump();
			if (text.size() > kMaximumManifestBytes) {
				return std::nullopt;
			}
			return text;
		} catch (...) {
			return std::nullopt;
		}
	}

	std::optional<std::string> ComputeFingerprint(const Manifest& a_manifest)
	{
		try {
			const auto normalized = NormalizeManifest(a_manifest);
			if (!normalized) {
				return std::nullopt;
			}
			std::string canonical{ kFingerprintPrefix };
			for (const auto& entry : normalized->entries) {
				canonical += kKindNames[static_cast<std::size_t>(entry.kind)];
				canonical += '\t';
				canonical += kPluginTypeNames[static_cast<std::size_t>(entry.pluginType)];
				canonical += '\t';
				canonical += std::to_string(entry.order);
				canonical += '\t';
				canonical += entry.name;
				canonical += '\t';
				canonical += entry.sha256Hex;
				canonical += '\t';
				if (entry.sizeBytes) {
					canonical += std::to_string(*entry.sizeBytes);
				}
				canonical += '\n';
			}
			return ContentManifest::Sha256Hex(canonical);
		} catch (...) {
			return std::nullopt;
		}
	}

	std::optional<std::string> MatchingFingerprint(const Manifest& a_required, const Manifest& a_client)
	{
		try {
			const auto required = ComputeFingerprint(a_required);
			if (!required || ComputeFingerprint(a_client) != required) {
				return std::nullopt;
			}
			return required;
		} catch (...) {
			return std::nullopt;
		}
	}

	std::optional<ManifestDiff> DiffManifests(const Manifest& a_required, const Manifest& a_client)
	{
		try {
			const auto required = NormalizeManifest(a_required);
			const auto client = NormalizeManifest(a_client);
			if (!required || !client) {
				return std::nullopt;
			}
			ManifestDiff diff;
			std::set<EntryKey> requiredKeys;
			for (const auto& entry : required->entries) {
				requiredKeys.insert(EntryKey{ entry.kind, entry.name });
			}
			std::map<EntryKey, const Entry*> clientByKey;
			for (const auto& entry : client->entries) {
				clientByKey.emplace(EntryKey{ entry.kind, entry.name }, &entry);
			}

			std::vector<std::pair<const Entry*, const Entry*>> common;
			for (const auto& entry : required->entries) {
				const auto found = clientByKey.find(EntryKey{ entry.kind, entry.name });
				if (found == clientByKey.end()) {
					diff.missing.push_back(entry);
					continue;
				}
				if (!SameContent(entry, *found->second)) {
					switch (entry.kind) {
					case EntryKind::plugin:
						diff.wrongContent.push_back(entry);
						break;
					case EntryKind::native:
						diff.wrongNative.push_back(entry);
						break;
					case EntryKind::config:
						diff.wrongConfig.push_back(entry);
						break;
					case EntryKind::archive:
						diff.wrongArchive.push_back(entry);
						break;
					}
				}
				if (entry.kind == EntryKind::plugin) {
					common.emplace_back(&entry, found->second);
				}
			}
			for (const auto& entry : client->entries) {
				if (!requiredKeys.contains(EntryKey{ entry.kind, entry.name })) {
					diff.unexpected.push_back(entry);
				}
			}

			auto byRequired = common;
			std::sort(byRequired.begin(), byRequired.end(), [](const auto& a_lhs, const auto& a_rhs) {
				return a_lhs.first->order < a_rhs.first->order;
			});
			auto byClient = common;
			std::sort(byClient.begin(), byClient.end(), [](const auto& a_lhs, const auto& a_rhs) {
				return a_lhs.second->order < a_rhs.second->order;
			});
			for (std::size_t index = 0; index < common.size(); ++index) {
				if (byRequired[index].first != byClient[index].first) {
					diff.wrongOrder.push_back(*byRequired[index].first);
				}
			}
			return diff;
		} catch (...) {
			return std::nullopt;
		}
	}

	std::string FormatMismatch(const ManifestDiff& a_diff)
	{
		try {
			const std::array<const std::vector<Entry>*, 7> sections{
				&a_diff.missing,
				&a_diff.unexpected,
				&a_diff.wrongContent,
				&a_diff.wrongNative,
				&a_diff.wrongConfig,
				&a_diff.wrongArchive,
				&a_diff.wrongOrder
			};
			std::string text{ kMismatchHeader };
			for (std::size_t index = 0; index < sections.size(); ++index) {
				const auto& entries = *sections[index];
				if (entries.empty()) {
					continue;
				}
				text += '\n';
				text += kSectionTitles[index];
				text += '\n';
				std::size_t shown = 0;
				for (const auto& entry : entries) {
					if (shown == kMaximumNamesPerSection) {
						break;
					}
					const auto line = "  " + entry.name + "\n";
					if (text.size() + line.size() + kMismatchReserveBytes > kMaximumMismatchTextBytes) {
						break;
					}
					text += line;
					++shown;
				}
				if (shown < entries.size()) {
					text += "  ...and " + std::to_string(entries.size() - shown) + " more\n";
				}
			}
			return text;
		} catch (...) {
			try {
				return std::string{ kMismatchHeader };
			} catch (...) {
				return std::string{ kMismatchFallback };
			}
		}
	}
}
