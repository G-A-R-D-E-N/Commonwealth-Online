#include "F4TWorldPolicy.h"
#include "F4TUtf8Path.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <charconv>
#include <cctype>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <system_error>
#include <unordered_set>
#include <utility>

namespace F4T::WorldPolicy
{
	namespace
	{
		using Json = nlohmann::json;

		struct FormKeyHash
		{
			std::size_t operator()(const FormKey& a_key) const noexcept
			{
				return std::hash<std::string>{}(a_key.plugin) ^
					(static_cast<std::size_t>(a_key.localId) * 0x9E3779B97F4A7C15ull);
			}
		};

		constexpr std::array<std::string_view, kClassCount> kClassNames{
			"story", "companion", "vendor", "town", "settler", "enemy", "animal"
		};

		bool IsPrintableAscii(std::string_view a_text) noexcept
		{
			return std::all_of(a_text.begin(), a_text.end(), [](unsigned char a_character) {
				return a_character >= 0x20 && a_character < 0x7F;
			});
		}

		bool ValidPluginName(std::string_view a_name) noexcept
		{
			return !a_name.empty() &&
				a_name.size() <= kMaximumPluginNameBytes &&
				a_name.find_first_of(":\"\\") == std::string_view::npos &&
				IsPrintableAscii(a_name);
		}

		bool HasOnlyKeys(const Json& a_object, std::initializer_list<std::string_view> a_keys)
		{
			if (!a_object.is_object()) {
				return false;
			}
			for (const auto& [name, value] : a_object.items()) {
				(void)value;
				const auto known = std::find(a_keys.begin(), a_keys.end(), std::string_view{ name });
				if (known == a_keys.end()) {
					return false;
				}
			}
			return true;
		}

		std::optional<std::uint32_t> ReadVersion(const Json& a_value, std::uint32_t a_minimum)
		{
			if (a_value.is_boolean()) {
				return std::nullopt;
			}
			std::uint64_t value = 0;
			if (a_value.is_number_unsigned()) {
				value = a_value.get<std::uint64_t>();
			} else if (a_value.is_number_integer()) {
				const auto signedValue = a_value.get<std::int64_t>();
				if (signedValue < 0) {
					return std::nullopt;
				}
				value = static_cast<std::uint64_t>(signedValue);
			} else {
				return std::nullopt;
			}
			if (value < a_minimum || value > kMaximumDataVersion) {
				return std::nullopt;
			}
			return static_cast<std::uint32_t>(value);
		}

		bool ReadFormKeyList(const Json& a_value, std::vector<FormKey>& a_out)
		{
			if (!a_value.is_array() || a_value.size() > kMaximumClassEntries) {
				return false;
			}
			a_out.reserve(a_value.size());
			std::unordered_set<FormKey, FormKeyHash> seen;
			seen.reserve(a_value.size());
			for (const auto& entry : a_value) {
				if (!entry.is_string()) {
					return false;
				}
				const auto key = ParseFormKey(entry.get_ref<const std::string&>());
				if (!key) {
					return false;
				}
				if (!seen.insert(*key).second) {
					return false;
				}
				a_out.push_back(*key);
			}
			return true;
		}

		std::optional<Data> ParseRoot(const Json& a_root)
		{
			if (!HasOnlyKeys(a_root, { "version", "game", "classes", "doors", "sample" })) {
				return std::nullopt;
			}
			const auto version = a_root.find("version");
			const auto game = a_root.find("game");
			const auto classes = a_root.find("classes");
			if (version == a_root.end() || game == a_root.end() || classes == a_root.end()) {
				return std::nullopt;
			}
			if (!game->is_string()) {
				return std::nullopt;
			}
			const auto& gameName = game->get_ref<const std::string&>();
			if (gameName.empty() || gameName.size() > kMaximumGameNameBytes || !IsPrintableAscii(gameName)) {
				return std::nullopt;
			}

			Data data;
			const auto dataVersion = ReadVersion(*version, 1);
			if (!dataVersion || !classes->is_object() || classes->size() != kClassCount) {
				return std::nullopt;
			}
			data.version = *dataVersion;
			data.game = gameName;
			for (std::size_t index = 0; index < kClassCount; ++index) {
				const auto found = classes->find(std::string{ kClassNames[index] });
				if (found == classes->end() || !ReadFormKeyList(*found, data.classes[index])) {
					return std::nullopt;
				}
			}
			if (const auto doors = a_root.find("doors"); doors != a_root.end()) {
				if (!ReadFormKeyList(*doors, data.doors)) {
					return std::nullopt;
				}
			}
			if (const auto sample = a_root.find("sample"); sample != a_root.end()) {
				if (!sample->is_boolean()) {
					return std::nullopt;
				}
				data.sample = sample->get<bool>();
			}
			return data;
		}
	}

	std::string_view ClassName(ClassId a_class) noexcept
	{
		const auto index = static_cast<std::size_t>(a_class);
		return index < kClassCount ? kClassNames[index] : std::string_view{};
	}

	std::optional<ClassId> ParseClass(std::string_view a_name) noexcept
	{
		const auto found = std::find(kClassNames.begin(), kClassNames.end(), a_name);
		if (found == kClassNames.end()) {
			return std::nullopt;
		}
		return static_cast<ClassId>(std::distance(kClassNames.begin(), found));
	}

	std::optional<FormKey> ParseFormKey(std::string_view a_text)
	{
		const auto separator = a_text.find(':');
		if (separator == std::string_view::npos) {
			return std::nullopt;
		}
		const auto idText = a_text.substr(0, separator);
		const auto plugin = a_text.substr(separator + 1);
		if (idText.size() != kFormKeyDigits || !ValidPluginName(plugin)) {
			return std::nullopt;
		}
		std::uint32_t localId = 0;
		const auto parsed = std::from_chars(idText.data(), idText.data() + idText.size(), localId, 16);
		if (parsed.ec != std::errc{} || parsed.ptr != idText.data() + idText.size()) {
			return std::nullopt;
		}
		if (localId == 0 || localId > kMaximumLocalId) {
			return std::nullopt;
		}
		return FormKey{ std::string{ plugin }, localId };
	}

	std::string FormatFormKey(const FormKey& a_key)
	{
		std::array<char, 7> form{};
		std::snprintf(form.data(), form.size(), "%06X", a_key.localId);
		return std::string{ form.data() } + ":" + a_key.plugin;
	}

	std::size_t Data::EntryCount() const noexcept
	{
		std::size_t total = doors.size();
		for (const auto& entries : classes) {
			total += entries.size();
		}
		return total;
	}

	std::optional<Data> ParseData(std::string_view a_bytes)
	{
		if (a_bytes.empty() || a_bytes.size() > kMaximumPolicyFileBytes) {
			return std::nullopt;
		}
		Json root;
		try {
			root = Json::parse(a_bytes);
		} catch (...) {
			return std::nullopt;
		}
		return ParseRoot(root);
	}

	std::optional<Data> LoadData(const std::filesystem::path& a_path)
	{
		std::ifstream stream{ a_path, std::ios::binary };
		if (!stream) {
			return std::nullopt;
		}
		std::string bytes{ std::istreambuf_iterator<char>{ stream }, std::istreambuf_iterator<char>{} };
		if (stream.bad()) {
			return std::nullopt;
		}
		return ParseData(bytes);
	}

	std::string_view ModeName(Mode a_mode) noexcept
	{
		switch (a_mode) {
		case Mode::CoopStory:
			return "coop-story";
		case Mode::FreeRoam:
			return "free-roam";
		}
		return "coop-story";
	}

	std::optional<Mode> ParseMode(std::string_view a_name) noexcept
	{
		constexpr std::size_t kMaximumModeNameBytes = 15;
		while (!a_name.empty() && (a_name.front() == ' ' || a_name.front() == '\t')) {
			a_name.remove_prefix(1);
		}
		while (!a_name.empty() && (a_name.back() == ' ' || a_name.back() == '\t')) {
			a_name.remove_suffix(1);
		}
		if (a_name.empty() || a_name.size() > kMaximumModeNameBytes) {
			return std::nullopt;
		}
		std::array<char, kMaximumModeNameBytes> normalized{};
		for (std::size_t index = 0; index < a_name.size(); ++index) {
			normalized[index] = static_cast<char>(std::tolower(static_cast<unsigned char>(a_name[index])));
		}
		const std::string_view name{ normalized.data(), a_name.size() };
		if (name == "coop-story") return Mode::CoopStory;
		if (name == "free-roam") return Mode::FreeRoam;
		return std::nullopt;
	}

	bool CanServeMode(const Data& a_data, Mode a_mode) noexcept
	{
		if (a_mode != Mode::FreeRoam) {
			return true;
		}
		if (a_data.sample) {
			return false;
		}
		return !a_data.classes[static_cast<std::size_t>(ClassId::Story)].empty() ||
			!a_data.classes[static_cast<std::size_t>(ClassId::Companion)].empty();
	}

	std::string UnservableModeMessage(std::string_view a_dataPath, Mode a_mode)
	{
		return "World mode " + std::string{ ModeName(a_mode) } +
			" needs a generated world policy file with story or companion entries; " + std::string{ a_dataPath } +
			" is missing, invalid or the placeholder sample. Generate the real data file "
			"(docs/protocol/world-policy.md) or set worldMode to coop-story.";
	}

	std::string UnservableModeError(const std::filesystem::path& a_dataPath, Mode a_mode)
	{
		if (CanServeMode(LoadData(a_dataPath).value_or(Data{}), a_mode)) {
			return {};
		}
		return UnservableModeMessage(F4T::Utf8Path::Encode(a_dataPath), a_mode);
	}

	std::string_view DoorsName(Doors a_doors) noexcept
	{
		return a_doors == Doors::Shared ? std::string_view{ "shared" } : std::string_view{ "vanilla" };
	}

	std::string_view VatsName(Vats a_vats) noexcept
	{
		switch (a_vats) {
		case Vats::Disabled:
			return "disabled";
		case Vats::RealTime:
			return "real-time";
		case Vats::Vanilla:
			return "vanilla";
		}
		return "disabled";
	}

	std::optional<Vats> ParseVats(std::string_view a_name) noexcept
	{
		constexpr std::size_t kMaximumVatsNameBytes = 15;
		while (!a_name.empty() && (a_name.front() == ' ' || a_name.front() == '\t')) {
			a_name.remove_prefix(1);
		}
		while (!a_name.empty() && (a_name.back() == ' ' || a_name.back() == '\t')) {
			a_name.remove_suffix(1);
		}
		if (a_name.empty() || a_name.size() > kMaximumVatsNameBytes) {
			return std::nullopt;
		}
		std::array<char, kMaximumVatsNameBytes> normalized{};
		for (std::size_t index = 0; index < a_name.size(); ++index) {
			normalized[index] = static_cast<char>(std::tolower(static_cast<unsigned char>(a_name[index])));
		}
		const std::string_view name{ normalized.data(), a_name.size() };
		if (name == "disabled") return Vats::Disabled;
		if (name == "real-time" || name == "realtime") return Vats::RealTime;
		if (name == "vanilla") return Vats::Vanilla;
		return std::nullopt;
	}

	std::optional<Vats> ParseWireVats(const nlohmann::json& a_packet)
	{
		const auto vats = a_packet.find("vats");
		const auto vatsMode = a_packet.find("vatsMode");
		if (vats == a_packet.end() && vatsMode == a_packet.end()) {
			return Vats::Vanilla;
		}
		if (vats != a_packet.end() && !vats->is_string()) {
			return std::nullopt;
		}
		if (vatsMode != a_packet.end() && !vatsMode->is_string()) {
			return std::nullopt;
		}
		const auto primary = vats != a_packet.end() ? ParseVats(vats->get_ref<const std::string&>()) : std::optional<Vats>{};
		const auto alias = vatsMode != a_packet.end() ? ParseVats(vatsMode->get_ref<const std::string&>()) : std::optional<Vats>{};
		if (vats != a_packet.end() && !primary) {
			return std::nullopt;
		}
		if (vatsMode != a_packet.end() && !alias) {
			return std::nullopt;
		}
		if (primary && alias && *primary != *alias) {
			return std::nullopt;
		}
		if (primary) {
			return primary;
		}
		return alias;
	}

	namespace
	{
		Vats& ServerDefaultVatsStorage() noexcept
		{
			static Vats s_default{ Vats::Disabled };
			return s_default;
		}
	}

	Vats ServerDefaultVats() noexcept
	{
		return ServerDefaultVatsStorage();
	}

	void SetServerDefaultVats(Vats a_vats) noexcept
	{
		ServerDefaultVatsStorage() = a_vats;
	}

	Policy DerivePolicy(const Data& a_data, Mode a_mode, bool a_doorsShared)
	{
		return DerivePolicy(a_data, a_mode, a_doorsShared, ServerDefaultVats());
	}

	Policy DerivePolicy(const Data& a_data, Mode a_mode, bool a_doorsShared, Vats a_vats)
	{
		Policy policy;
		policy.mode = a_mode;
		policy.version = a_data.version;
		policy.doors = a_doorsShared ? Doors::Shared : Doors::Vanilla;
		policy.suppressQuests = a_mode == Mode::FreeRoam;
		policy.vats = a_vats;
		if (a_mode != Mode::FreeRoam) {
			return policy;
		}
		policy.disable.reserve(
			a_data.classes[static_cast<std::size_t>(ClassId::Story)].size() +
			a_data.classes[static_cast<std::size_t>(ClassId::Companion)].size());
		std::unordered_set<FormKey, FormKeyHash> seen;
		for (const auto classId : { ClassId::Story, ClassId::Companion }) {
			for (const auto& key : a_data.classes[static_cast<std::size_t>(classId)]) {
				if (seen.insert(key).second) {
					policy.disable.push_back(key);
				}
			}
		}
		return policy;
	}

	Policy DerivePolicyV2(const Data& a_data, Mode a_mode, bool a_doorsShared, Vats a_vats)
	{
		auto policy = DerivePolicy(a_data, a_mode, a_doorsShared, a_vats);
		policy.noInteract = std::move(policy.disable);
		policy.disable.clear();
		return policy;
	}

	namespace
	{
		Json BaseMessage(const Policy& a_policy)
		{
			return Json{
			{ "type", "worldPolicy" },
			{ "mode", std::string{ ModeName(a_policy.mode) } },
			{ "version", a_policy.version },
			{ "doors", std::string{ DoorsName(a_policy.doors) } },
			{ "suppressQuests", a_policy.suppressQuests },
			{ "vats", std::string{ VatsName(a_policy.vats) } }
			};
		}

		Json DisableArray(const std::vector<FormKey>& a_keys, std::size_t a_begin, std::size_t a_end)
		{
			Json disable = Json::array();
			for (std::size_t index = a_begin; index < a_end; ++index) {
				disable.push_back(FormatFormKey(a_keys[index]));
			}
			return disable;
		}

		std::size_t PartCount(std::size_t a_entries, std::size_t a_cap) noexcept
		{
			return a_entries == 0 ? std::size_t{ 1 } : (a_entries + a_cap - 1) / a_cap;
		}

		Json PartArray(const std::vector<FormKey>& a_keys, std::size_t a_part, std::size_t a_cap)
		{
			const auto begin = std::min(a_part * a_cap, a_keys.size());
			return DisableArray(a_keys, begin, std::min(begin + a_cap, a_keys.size()));
		}
	}

	Json ToJson(const Policy& a_policy)
	{
		auto message = BaseMessage(a_policy);
		message["disable"] = DisableArray(a_policy.disable, 0, a_policy.disable.size());
		return message;
	}

	Json ToJsonV2(const Policy& a_policy)
	{
		auto message = ToJson(a_policy);
		message["noInteract"] = DisableArray(a_policy.noInteract, 0, a_policy.noInteract.size());
		message["blockedQuests"] = DisableArray(a_policy.blockedQuests, 0, a_policy.blockedQuests.size());
		return message;
	}

	std::vector<Json> EncodeMessages(const Policy& a_policy)
	{
		return EncodeMessages(a_policy, false);
	}

	std::vector<Json> EncodeMessages(const Policy& a_policy, bool a_v2)
	{
		const auto cap = a_v2 ? kMaximumV2EntriesPerMessage : kMaximumDisableEntriesPerMessage;
		const auto parts = a_v2
			? std::max({ PartCount(a_policy.disable.size(), cap), PartCount(a_policy.noInteract.size(), cap), PartCount(a_policy.blockedQuests.size(), cap) })
			: PartCount(a_policy.disable.size(), cap);
		std::vector<Json> messages;
		messages.reserve(parts);
		for (std::size_t part = 0; part < parts; ++part) {
			auto message = BaseMessage(a_policy);
			if (parts > 1) {
				message["type"] = std::string{ kPartialMessageType };
				message["part"] = part;
				message["parts"] = parts;
			}
			message["disable"] = PartArray(a_policy.disable, part, cap);
			if (a_v2) {
				message["noInteract"] = PartArray(a_policy.noInteract, part, cap);
				message["blockedQuests"] = PartArray(a_policy.blockedQuests, part, cap);
			}
			messages.push_back(std::move(message));
		}
		return messages;
	}

	std::optional<Packet> DecodeMessage(const Json& a_packet)
	{
		std::size_t part = 0;
		std::size_t parts = 1;
		const auto type = a_packet.find("type");
		if (type != a_packet.end()) {
			if (!type->is_string()) {
				return std::nullopt;
			}
			const auto& typeName = type->get_ref<const std::string&>();
			if (typeName == kPartialMessageType) {
				const auto partNode = a_packet.find("part");
				const auto partsNode = a_packet.find("parts");
				if (partNode == a_packet.end() || partsNode == a_packet.end() ||
					!partNode->is_number_unsigned() || !partsNode->is_number_unsigned()) {
					return std::nullopt;
				}
				part = partNode->get<std::size_t>();
				parts = partsNode->get<std::size_t>();
				if (parts < 2 || part >= parts) {
					return std::nullopt;
				}
			} else if (typeName != "worldPolicy") {
				return std::nullopt;
			}
		}
		if (parts > 1) {
			if (!HasOnlyKeys(a_packet, { "type", "part", "parts", "mode", "version", "disable", "noInteract", "blockedQuests", "doors", "suppressQuests", "vats", "vatsMode" })) {
				return std::nullopt;
			}
		} else if (!HasOnlyKeys(a_packet, { "type", "mode", "version", "disable", "noInteract", "blockedQuests", "doors", "suppressQuests", "vats", "vatsMode" })) {
			return std::nullopt;
		}
		const auto mode = a_packet.find("mode");
		const auto version = a_packet.find("version");
		const auto disable = a_packet.find("disable");
		const auto doors = a_packet.find("doors");
		const auto suppressQuests = a_packet.find("suppressQuests");
		if (mode == a_packet.end() || version == a_packet.end() || disable == a_packet.end() ||
			doors == a_packet.end() || suppressQuests == a_packet.end()) {
			return std::nullopt;
		}
		if (!mode->is_string() || !doors->is_string() || !suppressQuests->is_boolean()) {
			return std::nullopt;
		}
		const auto parsedMode = ParseMode(mode->get_ref<const std::string&>());
		if (!parsedMode) {
			return std::nullopt;
		}
		const auto parsedVersion = ReadVersion(*version, 0);
		if (!parsedVersion) {
			return std::nullopt;
		}

		Packet packet;
		packet.part = part;
		packet.parts = parts;
		packet.policy.mode = *parsedMode;
		packet.policy.version = *parsedVersion;
		packet.policy.suppressQuests = suppressQuests->get<bool>();
		const auto& doorsName = doors->get_ref<const std::string&>();
		if (doorsName == "shared") {
			packet.policy.doors = Doors::Shared;
		} else if (doorsName == "vanilla") {
			packet.policy.doors = Doors::Vanilla;
		} else {
			return std::nullopt;
		}
		const auto wireVats = ParseWireVats(a_packet);
		if (!wireVats) {
			return std::nullopt;
		}
		packet.policy.vats = *wireVats;
		if (!ReadFormKeyList(*disable, packet.policy.disable)) {
			return std::nullopt;
		}
		if (const auto noInteract = a_packet.find("noInteract"); noInteract != a_packet.end()) {
			if (!ReadFormKeyList(*noInteract, packet.policy.noInteract)) {
				return std::nullopt;
			}
		}
		if (const auto blockedQuests = a_packet.find("blockedQuests"); blockedQuests != a_packet.end()) {
			if (!ReadFormKeyList(*blockedQuests, packet.policy.blockedQuests) ||
				packet.policy.blockedQuests.size() > kMaximumBlockedQuests) {
				return std::nullopt;
			}
		}
		const auto v2Parts = a_packet.contains("noInteract") || a_packet.contains("blockedQuests");
		const auto cap = v2Parts ? kMaximumV2EntriesPerMessage : kMaximumDisableEntriesPerMessage;
		const auto maximumParts = v2Parts ? kMaximumV2MessageParts : kMaximumMessageParts;
		if (parts > 1 && (parts > maximumParts || packet.policy.disable.size() > cap ||
			packet.policy.noInteract.size() > cap ||
			packet.policy.blockedQuests.size() > cap)) {
			return std::nullopt;
		}
		return packet;
	}

	std::optional<Policy> ParsePolicy(const Json& a_packet)
	{
		const auto packet = DecodeMessage(a_packet);
		if (!packet || packet->parts != 1) {
			return std::nullopt;
		}
		return packet->policy;
	}

	void Reassembler::Reset() noexcept
	{
		m_base = Policy{};
		m_disable.clear();
		m_noInteract.clear();
		m_blockedQuests.clear();
		m_parts = 0;
		m_received = 0;
	}

	std::optional<Policy> Reassembler::Apply(const Json& a_packet)
	{
		const auto decoded = DecodeMessage(a_packet);
		if (!decoded) {
			return std::nullopt;
		}
		if (decoded->parts == 1) {
			Reset();
			return decoded->policy;
		}
		auto base = decoded->policy;
		base.disable.clear();
		base.noInteract.clear();
		base.blockedQuests.clear();
		const auto continues = m_received != 0 && m_parts == decoded->parts && m_base == base &&
			decoded->part == m_received;
		if (!continues) {
			Reset();
			if (decoded->part != 0) {
				return std::nullopt;
			}
			m_base = std::move(base);
			m_parts = decoded->parts;
		}
		if (m_disable.size() + decoded->policy.disable.size() > kMaximumTotalDisableEntries ||
			m_noInteract.size() + decoded->policy.noInteract.size() > kMaximumTotalDisableEntries ||
			m_blockedQuests.size() + decoded->policy.blockedQuests.size() > kMaximumBlockedQuests) {
			Reset();
			return std::nullopt;
		}
		m_disable.insert(m_disable.end(), decoded->policy.disable.begin(), decoded->policy.disable.end());
		m_noInteract.insert(m_noInteract.end(), decoded->policy.noInteract.begin(), decoded->policy.noInteract.end());
		m_blockedQuests.insert(m_blockedQuests.end(), decoded->policy.blockedQuests.begin(), decoded->policy.blockedQuests.end());
		++m_received;
		if (m_received != m_parts) {
			return std::nullopt;
		}
		auto complete = m_base;
		complete.disable = std::move(m_disable);
		complete.noInteract = std::move(m_noInteract);
		complete.blockedQuests = std::move(m_blockedQuests);
		Reset();
		return complete;
	}
}
