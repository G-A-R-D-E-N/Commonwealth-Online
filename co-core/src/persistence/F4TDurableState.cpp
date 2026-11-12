#include "F4TDurableState.h"

#include "F4TPlayerStateValidation.h"
#include "F4TProgressionValidation.h"
#include "F4TDropLedger.h"
#include "F4TCharacterSlots.h"
#include "F4TDeathIntent.h"
#include "F4TDeathBag.h"

#include <array>
#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace F4T::DurableState
{
	namespace
	{
		using Json = nlohmann::json;

		bool IsHex(std::string_view a_value) noexcept
		{
			for (const char ch : a_value) {
				if (!((ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'f') || (ch >= 'A' && ch <= 'F'))) {
					return false;
				}
			}
			return true;
		}

		std::optional<std::uint32_t> ReadUInt32(const Json& a_value)
		{
			if (a_value.is_boolean()) {
				return std::nullopt;
			}
			try {
				if (a_value.is_number_unsigned()) {
					const auto value = a_value.get<std::uint64_t>();
					if (value <= (std::numeric_limits<std::uint32_t>::max)()) {
						return static_cast<std::uint32_t>(value);
					}
				}
				if (a_value.is_number_integer()) {
					const auto value = a_value.get<std::int64_t>();
					if (value >= 0 && static_cast<std::uint64_t>(value) <= (std::numeric_limits<std::uint32_t>::max)()) {
						return static_cast<std::uint32_t>(value);
					}
				}
			} catch (...) {
			}
			return std::nullopt;
		}

		std::optional<std::uint32_t> ReadAttestationSlot(const Json& a_record)
		{
			const auto slot = a_record.is_object() ? a_record.find("slot") : a_record.end();
			return slot != a_record.end() ? ReadUInt32(*slot) : std::nullopt;
		}

		std::optional<double> ReadNonNegativeDouble(const Json& a_value)
		{
			if (a_value.is_boolean() || !a_value.is_number()) {
				return std::nullopt;
			}
			try {
				const auto value = a_value.get<double>();
				if (std::isfinite(value) && value >= 0.0) {
					return value;
				}
			} catch (...) {
			}
			return std::nullopt;
		}

		Json* Profile(Json& a_root, std::string_view a_profileId, bool a_create, double a_updatedAt)
		{
			auto profiles = a_root.find("profiles");
			if (profiles == a_root.end() || !profiles->is_object()) {
				throw std::invalid_argument("Durable profiles object is missing.");
			}
			const std::string profileId{ a_profileId };
			auto found = profiles->find(profileId);
			if (found != profiles->end()) {
				if (!found->is_object()) {
					throw std::invalid_argument("Durable profile must be an object.");
				}
				return std::addressof(*found);
			}
			if (!a_create) {
				return nullptr;
			}
			(*profiles)[profileId] = Json{ { "updatedAt", a_updatedAt } };
			return std::addressof((*profiles)[profileId]);
		}

		const Json* Profile(const Json& a_root, std::string_view a_profileId)
		{
			const auto profiles = a_root.find("profiles");
			if (profiles == a_root.end() || !profiles->is_object()) {
				throw std::invalid_argument("Durable profiles object is missing.");
			}
			const auto found = profiles->find(std::string{ a_profileId });
			if (found == profiles->end()) {
				return nullptr;
			}
			if (!found->is_object()) {
				throw std::invalid_argument("Durable profile must be an object.");
			}
			return std::addressof(*found);
		}

		void DropRejectedProgression(Json& a_playerState, const Json& a_root, std::string_view a_profileId, double a_nowSeconds)
		{
			const auto candidate = a_playerState.find("progression");
			if (candidate == a_playerState.end()) {
				return;
			}
			const auto* profile = Profile(a_root, a_profileId);
			const Json empty = Json::object();
			const auto& previous = profile != nullptr && profile->contains("playerState") && (*profile)["playerState"].is_object() ?
				(*profile)["playerState"] :
				empty;
			if (!F4T::ProgressionValidation::IsProgressionAccepted(previous, *candidate, a_nowSeconds)) {
				a_playerState.erase("progression");
			}
		}

		std::string HashToHex(const F4T::Identity::TokenHash& a_hash)
		{
			static constexpr char hex[] = "0123456789abcdef";
			std::string output;
			output.resize(a_hash.size() * 2);
			for (std::size_t index = 0; index < a_hash.size(); ++index) {
				output[index * 2] = hex[(a_hash[index] >> 4) & 0x0f];
				output[index * 2 + 1] = hex[a_hash[index] & 0x0f];
			}
			return output;
		}

		F4T::Identity::TokenHash HexToHash(std::string_view a_hex)
		{
			if (a_hex.size() != 64 || !IsHex(a_hex)) {
				throw std::invalid_argument("Invalid persisted resume hash.");
			}
			F4T::Identity::TokenHash hash{};
			for (std::size_t index = 0; index < hash.size(); ++index) {
				const auto pair = a_hex.substr(index * 2, 2);
				unsigned int value = 0;
				const auto result = std::from_chars(pair.data(), pair.data() + pair.size(), value, 16);
				if (result.ec != std::errc{} || result.ptr != pair.data() + pair.size()) {
					throw std::invalid_argument("Invalid persisted resume hash.");
				}
				hash[index] = static_cast<std::uint8_t>(value);
			}
			return hash;
		}
	}

	nlohmann::json NormalizeStartingInventory(const nlohmann::json& a_catalog)
	{
		if (!a_catalog.is_array() || a_catalog.size() > MaximumStartingInventoryItems) {
			throw std::invalid_argument("starting_inventory must be an array with at most 2048 entries.");
		}
		Json items = Json::object();
		for (const auto& entry : a_catalog) {
			if (!entry.is_object() || entry.size() != 3 || !entry.contains("plugin") ||
				!entry.at("plugin").is_string() || !entry.contains("localFormId") ||
				!entry.at("localFormId").is_string() || !entry.contains("count")) {
				throw std::invalid_argument("Starting inventory entries require plugin, localFormId and count only.");
			}
			auto plugin = entry.at("plugin").get<std::string>();
			const auto form = entry.at("localFormId").get<std::string>();
			const auto count = ReadUInt32(entry.at("count"));
			if (plugin.empty() || plugin.size() > 128 || form.empty() || form.size() > 8 || !IsHex(form) ||
				!count || *count == 0) {
				throw std::invalid_argument("Starting inventory descriptor or count is invalid.");
			}
			for (char& ch : plugin) {
				if (static_cast<unsigned char>(ch) < 0x20 || static_cast<unsigned char>(ch) >= 0x7f ||
					ch == '/' || ch == '\\' || ch == ':') {
					throw std::invalid_argument("Starting inventory plugin must be an ASCII filename.");
				}
				if (ch >= 'A' && ch <= 'Z') ch = static_cast<char>(ch + ('a' - 'A'));
			}
			std::uint32_t localId{};
			const auto parsed = std::from_chars(form.data(), form.data() + form.size(), localId, 16);
			if (parsed.ec != std::errc{} || localId == 0) {
				throw std::invalid_argument("Starting inventory localFormId must be nonzero hex.");
			}
			std::array<char, 9> normalized{};
			std::snprintf(normalized.data(), normalized.size(), "%08X", localId);
			const auto key = plugin + ":" + normalized.data();
			if (items.contains(key)) throw std::invalid_argument("Starting inventory contains a duplicate item.");
			items[key] = Json{ { "plugin", plugin }, { "localFormId", normalized.data() }, { "count", *count } };
		}
		if (items.dump().size() > MaximumStartingInventoryDumpBytes) {
			throw std::invalid_argument("Starting inventory exceeds the 1 MiB serialized catalog limit.");
		}
		return items;
	}

	void ValidateKeyedItems(const nlohmann::json& a_items)
	{
		Json catalog = Json::array();
		for (const auto& item : a_items) catalog.push_back(item);
		if (NormalizeStartingInventory(catalog) != a_items) {
			throw std::invalid_argument("Durable inventory catalog keys are invalid.");
		}
	}

	void ValidateInventory(const nlohmann::json& a_inventory)
	{
		if (!a_inventory.is_object() || a_inventory.size() != 3 ||
			!a_inventory.contains("initialized") || !a_inventory.at("initialized").is_boolean() || a_inventory.at("initialized") != true ||
			!a_inventory.contains("revision") || !ReadUInt32(a_inventory.at("revision")) ||
			!a_inventory.contains("items") || !a_inventory.at("items").is_object()) {
			throw std::invalid_argument("Durable inventory record is invalid.");
		}
		ValidateKeyedItems(a_inventory.at("items"));
	}

	bool InitializeInventory(nlohmann::json& a_root, std::string_view a_profileId,
		const nlohmann::json& a_catalog, double a_updatedAt)
	{
		const auto items = NormalizeStartingInventory(a_catalog);
		const auto slotKey = F4T::CharacterSlots::ParseSlotKey(a_profileId);
		const auto* account = (slotKey && F4T::CharacterSlots::IsValidSlot(slotKey->second, F4T::CharacterSlots::MaximumCharacterSlots)) ? Profile(std::as_const(a_root), slotKey->first) : nullptr;
		const bool slotAdmitted = account != nullptr && account->contains("resume");
		auto* profile = Profile(a_root, a_profileId, slotAdmitted, a_updatedAt);
		const bool admitted = profile != nullptr && (slotAdmitted || profile->contains("resume"));
		if (!admitted) {
			throw std::invalid_argument("Inventory grant requires an admitted durable profile.");
		}
		if (profile->contains("inventory")) return false;
		if (a_root.at("inventoryAuthorityBlocked").get<bool>()) {
			throw std::runtime_error("Inventory authority is blocked after durable backup recovery.");
		}
		(*profile)["inventory"] = Json{ { "initialized", true }, { "revision", 0u }, { "items", items } };
		(*profile)["inventoryOperations"] = Json::array();
		(*profile)["updatedAt"] = a_updatedAt;
		if (!items.empty()) (*profile)["startingGrant"] = Json{ { "granted", false }, { "items", items }, { "recovery", Json{ { "state", "unstarted" } } } };
		return true;
	}

	std::optional<nlohmann::json> GetInventory(const nlohmann::json& a_root, std::string_view a_profileId)
	{
		const auto* profile = Profile(a_root, a_profileId);
		if (profile == nullptr || !profile->contains("inventory")) return std::nullopt;
		return profile->at("inventory");
	}

	std::optional<nlohmann::json> GetStartingGrant(const nlohmann::json& a_root, std::string_view a_profileId)
	{
		const auto* profile = Profile(a_root, a_profileId);
		if (profile == nullptr || !profile->contains("startingGrant")) return std::nullopt;
		return profile->at("startingGrant");
	}

	std::optional<nlohmann::json> PendingStartingGrant(const nlohmann::json& a_root, std::string_view a_profileId)
	{
		const auto stored = GetStartingGrant(a_root, a_profileId);
		if (!stored) return std::nullopt;
		const auto& grant = *stored;
		if (grant.contains("recovery") && grant.at("recovery").value("state", std::string{}) == "resolved") return std::nullopt;
		Json items = Json::array();
		for (const auto& item : grant.at("items")) items.push_back(item);
		return Json{ { "grantId", std::string{ kStartingGrantId } }, { "items", items },
			{ "state", grant.contains("recovery") ? grant.at("recovery").at("state") : Json("legacy") } };
	}

	bool BeginStartingGrant(nlohmann::json& a_root, std::string_view a_profileId,
		std::string_view a_characterId, const F4T::SaveProfileLogic::AttestedSave& a_baseline)
	{
		ValidateSaveAttestation(a_baseline);
		if (a_root.at("inventoryAuthorityBlocked").get<bool>()) return false;
		if (!F4T::SaveProfileLogic::CanRebindProfileSave(a_characterId, a_baseline.saveFile)) return false;
		const auto slot = F4T::CharacterSlots::ParseSlotKey(a_profileId);
		const auto attestations = GetSaveAttestations(a_root, slot ? slot->first : std::string{ a_profileId });
		std::size_t characterSaves = 0;
		bool baselineRecorded = false;
		for (const auto& recorded : attestations) {
			if (!F4T::SaveProfileLogic::CanRebindProfileSave(a_characterId, recorded.saveFile)) continue;
			++characterSaves;
			baselineRecorded = F4T::SaveProfileLogic::SameAttestedSave(recorded, a_baseline);
		}
		if (characterSaves != 1 || !baselineRecorded) return false;
		auto* profile = Profile(a_root, a_profileId, false, 0.0);
		if (!profile || !profile->contains("startingGrant")) return false;
		auto& grant = (*profile)["startingGrant"];
		if (grant.at("granted") != false || !grant.contains("recovery") ||
			grant.at("recovery") != Json{ { "state", "unstarted" } }) return false;
		grant["recovery"] = Json{ { "state", "intent" }, { "characterId", a_characterId },
			{ "baselineSave", a_baseline.saveFile }, { "baselineSha256", a_baseline.sha256Hex } };
		return true;
	}

	bool ConsumeStartingGrantApproval(nlohmann::json& a_root, std::string_view a_profileId, const F4T::SaveProfileLogic::AttestedSave& a_save)
	{
		ValidateSaveAttestation(a_save);
		if (a_root.at("inventoryAuthorityBlocked").get<bool>()) return false;
		auto* profile = Profile(a_root, a_profileId, false, 0.0);
		if (!profile || !profile->contains("inventory") || !profile->contains("startingGrant")) return false;
		auto& grant = profile->at("startingGrant");
		if (!grant.contains("recovery")) return false;
		auto& recovery = grant.at("recovery");
		if (recovery.value("state", std::string{}) != "resolved" || recovery.value("consumed", false) ||
			recovery.at("inventoryRevision") != profile->at("inventory").at("revision")) return false;
		const F4T::SaveProfileLogic::AttestedSave approved{ recovery.at("saveFile").get<std::string>(), recovery.at("sha256").get<std::string>(), 0 };
		if (!F4T::SaveProfileLogic::SameAttestedSave(approved, a_save)) return false;
		recovery["consumed"] = true;
		return true;
	}
	bool ResolveStartingGrant(nlohmann::json& a_root, std::string_view a_profileId,
		std::string_view a_characterId, const F4T::SaveProfileLogic::AttestedSave& a_save, std::string_view a_note, bool a_legacy)
	{
		ValidateSaveAttestation(a_save);
		if (a_root.at("inventoryAuthorityBlocked").get<bool>()) return false;
		if (a_note.empty() || a_note.size() > 1024 || !F4T::SaveProfileLogic::CanRebindProfileSave(a_characterId, a_save.saveFile)) return false;
		auto* profile = Profile(a_root, a_profileId, false, 0.0);
		if (!profile || !profile->contains("startingGrant") || !profile->contains("inventory")) return false;
		auto& grant = (*profile)["startingGrant"];
		if (a_legacy == grant.contains("recovery")) return false;
		Json intent = nullptr;
		if (grant.contains("recovery")) {
			const auto& recovery = grant.at("recovery");
			if (recovery.value("state", std::string{}) != "unstarted" && recovery.value("state", std::string{}) != "intent" && recovery.value("state", std::string{}) != "resolved") return false;
			if (recovery.value("state", std::string{}) != "unstarted" && !F4T::SaveProfileLogic::CharacterIdsEqual(recovery.at("characterId").get<std::string>(), a_characterId)) return false;
			if (recovery.value("state", std::string{}) == "intent") {
				if (F4T::SaveProfileLogic::EqualsIgnoreCase(recovery.at("baselineSha256").get<std::string>(), a_save.sha256Hex)) return false;
				intent = recovery;
			} else if (recovery.value("state", std::string{}) == "resolved") {
				if (F4T::SaveProfileLogic::EqualsIgnoreCase(recovery.at("saveFile").get<std::string>(), a_save.saveFile) &&
					F4T::SaveProfileLogic::EqualsIgnoreCase(recovery.at("sha256").get<std::string>(), a_save.sha256Hex) &&
					recovery.at("inventoryRevision") == profile->at("inventory").at("revision") && !recovery.value("consumed", false)) return false;
				intent = recovery.at("intent");
			} else if (recovery.value("state", std::string{}) != "unstarted") return false;
		}
		grant["granted"] = true;
		grant["recovery"] = Json{ { "state", "resolved" }, { "characterId", a_characterId },
			{ "saveFile", a_save.saveFile }, { "sha256", a_save.sha256Hex }, { "note", a_note }, { "intent", intent },
			{ "inventoryRevision", profile->at("inventory").at("revision") }, { "consumed", false } };
		return true;
	}

	nlohmann::json NewRoot(std::string_view a_serverInstanceId, double a_savedAt)
	{
		if (a_serverInstanceId.size() != 32 || !IsHex(a_serverInstanceId) || !std::isfinite(a_savedAt)) {
			throw std::invalid_argument("Durable state requires a 32-character hex serverInstanceId and finite savedAt.");
		}
		return Json{
			{ "schemaVersion", CurrentSchemaVersion },
			{ "serverInstanceId", std::string{ a_serverInstanceId } },
			{ "savedAt", a_savedAt },
			{ "profiles", Json::object() },
			{ "worldState", Json::object() }, { "containers", Json::object() },
			{ "inventoryAuthorityBlocked", false },
			{ "drops", Json{ { "nextEntityId", F4T::Drops::kFirstDroppedEntityId }, { "live", Json::object() } } }
		};
	}

	LoadResult LoadAndMigrate(nlohmann::json a_root, double a_savedAt)
	{
		if (!a_root.is_object()) {
			throw std::invalid_argument("Durable state root must be a JSON object.");
		}
		const auto schemaIt = a_root.find("schemaVersion");
		const auto schema = schemaIt == a_root.end() ? std::nullopt : ReadUInt32(*schemaIt);
		if (!schema || *schema < 1) {
			throw std::invalid_argument("Durable state is missing a valid schemaVersion.");
		}
		if (*schema > CurrentSchemaVersion) {
			throw std::invalid_argument("Durable state schema is newer than supported schema.");
		}

		bool migrated = false;
		if (*schema == 1) {
			const auto instanceIt = a_root.contains("serverInstanceId") ? a_root.find("serverInstanceId") : a_root.find("serverId");
			if (instanceIt == a_root.end() || !instanceIt->is_string() || instanceIt->get_ref<const std::string&>().empty()) {
				throw std::invalid_argument("Schema 1 durable state is missing serverId.");
			}
			Json profiles = Json::object();
			if (const auto found = a_root.find("profiles"); found != a_root.end() && found->is_object()) {
				profiles = *found;
			} else if (const auto found = a_root.find("players"); found != a_root.end() && found->is_object()) {
				profiles = *found;
			}
			Json world = Json::object();
			if (const auto found = a_root.find("worldState"); found != a_root.end() && found->is_object()) {
				world = *found;
			}
			a_root = Json{
				{ "schemaVersion", CurrentSchemaVersion },
				{ "serverInstanceId", instanceIt->get<std::string>() },
				{ "savedAt", a_savedAt },
				{ "profiles", std::move(profiles) },
				{ "worldState", std::move(world) },
				{ "inventoryAuthorityBlocked", false },
			{ "drops", Json{ { "nextEntityId", F4T::Drops::kFirstDroppedEntityId }, { "live", Json::object() } } }
			};
			migrated = true;
		}

		if (*schema == 2) {
			a_root["schemaVersion"] = CurrentSchemaVersion;
			a_root["inventoryAuthorityBlocked"] = false;
			migrated = true;
		}

		if (*schema == 2 || *schema == 3) {
			migrated = true;
			if (a_root.contains("drops")) throw std::invalid_argument("Older schema contains unsupported drop authority data.");
			a_root["schemaVersion"] = CurrentSchemaVersion;
			a_root["drops"] = Json{ { "nextEntityId", F4T::Drops::kFirstDroppedEntityId }, { "live", Json::object() } };
			for (auto& profile : a_root.at("profiles")) {
				if (profile.contains("inventory")) {
					if (*schema != 3 || profile.at("inventory").at("revision") != 0 || profile.contains("inventoryOperations")) throw std::invalid_argument("Older inventory cannot be migrated without transaction history.");
					profile["inventoryOperations"] = Json::array();
				}
			}
		}
		if (*schema < 5) {
			a_root["schemaVersion"] = CurrentSchemaVersion;
			a_root["containers"] = Json::object();
			migrated = true;
		}
		if (*schema == 5) {
			a_root["schemaVersion"] = CurrentSchemaVersion;
			migrated = true;
		}
		F4T::DeathBag::ValidateGrantHistory(a_root);
		ValidateRoot(a_root);
		return LoadResult{ std::move(a_root), migrated };
	}

	void ValidateRoot(const nlohmann::json& a_root)
	{
		if (!a_root.is_object()) {
			throw std::invalid_argument("Durable state root must be a JSON object.");
		}
		const auto schemaIt = a_root.find("schemaVersion");
		const auto schema = schemaIt == a_root.end() ? std::nullopt : ReadUInt32(*schemaIt);
		if (!schema || *schema != CurrentSchemaVersion) {
			throw std::invalid_argument("Durable state must use schema 6 after migration.");
		}
		if (!a_root.contains("inventoryAuthorityBlocked") || !a_root.at("inventoryAuthorityBlocked").is_boolean()) {
			throw std::invalid_argument("Durable inventory recovery marker is missing or invalid.");
		}
		const auto instanceIt = a_root.find("serverInstanceId");
		if (instanceIt == a_root.end() || !instanceIt->is_string()) {
			throw std::invalid_argument("Durable state serverInstanceId must be a 32-character hex identifier.");
		}
		const auto instance = instanceIt->get<std::string>();
		if (instance.size() != 32 || !IsHex(instance)) {
			throw std::invalid_argument("Durable state serverInstanceId must be a 32-character hex identifier.");
		}
		const auto savedIt = a_root.find("savedAt");
		if (savedIt == a_root.end() || !ReadNonNegativeDouble(*savedIt)) {
			throw std::invalid_argument("Durable state savedAt must be a finite timestamp.");
		}
		const auto profilesIt = a_root.find("profiles");
		if (profilesIt == a_root.end() || !profilesIt->is_object()) {
			throw std::invalid_argument("Durable state profiles must be an object.");
		}
		const auto worldIt = a_root.find("worldState");
		if (worldIt == a_root.end() || !worldIt->is_object()) {
			throw std::invalid_argument("Durable state worldState must be an object.");
		}

		for (auto profileIt = profilesIt->begin(); profileIt != profilesIt->end(); ++profileIt) {
			if (!profileIt.value().is_object()) {
				throw std::invalid_argument("Durable profile must be an object.");
			}
			const auto& profile = profileIt.value();
			if (const auto inventory = profile.find("inventory"); inventory != profile.end()) {
				ValidateInventory(*inventory);
			}
			if (const auto grant = profile.find("startingGrant"); grant != profile.end()) {
				if (!grant->is_object() || (grant->size() != 2 && grant->size() != 3) || !grant->contains("granted") || !grant->at("granted").is_boolean() ||
					!grant->contains("items") || !grant->at("items").is_object()) {
					throw std::invalid_argument("Durable starting grant is invalid.");
				}
				ValidateKeyedItems(grant->at("items"));
				if (grant->size() == 3) {
					const auto recovery = grant->find("recovery");
					if (recovery == grant->end() || !recovery->is_object()) throw std::invalid_argument("Invalid starting grant recovery.");
					if (*recovery == Json{ { "state", "unstarted" } } && grant->at("granted") != false) throw std::invalid_argument("Invalid starting grant state.");
					if (*recovery != Json{ { "state", "unstarted" } }) {
						if (recovery->value("state", std::string{}) == "resolved") {
							if (grant->at("granted") != true || (recovery->size() != 7 && recovery->size() != 8) || (recovery->size() == 8 && !recovery->contains("consumed")) || (recovery->contains("consumed") && !recovery->at("consumed").is_boolean()) || !recovery->contains("intent") ||
								!recovery->contains("inventoryRevision") || !ReadUInt32(recovery->at("inventoryRevision")) ||
								!recovery->contains("characterId") || !recovery->at("characterId").is_string() ||
								!recovery->contains("saveFile") || !recovery->at("saveFile").is_string() ||
								!recovery->contains("sha256") || !recovery->at("sha256").is_string() ||
								!recovery->contains("note") || !recovery->at("note").is_string() || recovery->at("note").get_ref<const std::string&>().empty() ||
								recovery->at("note").get_ref<const std::string&>().size() > 1024) throw std::invalid_argument("Invalid starting grant resolution.");
							const F4T::SaveProfileLogic::AttestedSave save{ recovery->at("saveFile").get<std::string>(), recovery->at("sha256").get<std::string>(), 0 };
							ValidateSaveAttestation(save);
							if (!F4T::SaveProfileLogic::CanRebindProfileSave(recovery->at("characterId").get<std::string>(), save.saveFile)) throw std::invalid_argument("Starting grant character mismatch.");
							const auto& intent = recovery->at("intent");
							if (!intent.is_null()) {
								if (!intent.is_object() || intent.size() != 4 || intent.value("state", std::string{}) != "intent" ||
									!intent.contains("characterId") || !intent.at("characterId").is_string() ||
									!intent.contains("baselineSave") || !intent.at("baselineSave").is_string() ||
									!intent.contains("baselineSha256") || !intent.at("baselineSha256").is_string()) throw std::invalid_argument("Invalid starting grant intent.");
								const F4T::SaveProfileLogic::AttestedSave baseline{ intent.at("baselineSave").get<std::string>(), intent.at("baselineSha256").get<std::string>(), 0 };
								ValidateSaveAttestation(baseline);
								if (!F4T::SaveProfileLogic::CanRebindProfileSave(intent.at("characterId").get<std::string>(), baseline.saveFile) ||
									!F4T::SaveProfileLogic::CharacterIdsEqual(intent.at("characterId").get<std::string>(), recovery->at("characterId").get<std::string>()) ||
									F4T::SaveProfileLogic::EqualsIgnoreCase(baseline.sha256Hex, save.sha256Hex)) throw std::invalid_argument("Invalid starting grant resolution.");
							}
						} else {
						if (grant->at("granted") != false || recovery->size() != 4 || recovery->value("state", std::string{}) != "intent" ||
							!recovery->contains("characterId") || !recovery->at("characterId").is_string() ||
							!recovery->contains("baselineSave") || !recovery->at("baselineSave").is_string() ||
							!recovery->contains("baselineSha256") || !recovery->at("baselineSha256").is_string()) throw std::invalid_argument("Invalid starting grant intent.");
						const F4T::SaveProfileLogic::AttestedSave baseline{ recovery->at("baselineSave").get<std::string>(), recovery->at("baselineSha256").get<std::string>(), 0 };
						ValidateSaveAttestation(baseline);
						if (!F4T::SaveProfileLogic::CanRebindProfileSave(recovery->at("characterId").get<std::string>(), baseline.saveFile)) throw std::invalid_argument("Starting grant character mismatch.");
						}
					}
				}
			}
			if (const auto resume = profile.find("resume"); resume != profile.end()) {
				if (!resume->is_object()) {
					throw std::invalid_argument("Durable resume record must be an object.");
				}
				const auto hashIt = resume->find("tokenHash");
				const auto expiryIt = resume->find("expiresAt");
				if (hashIt == resume->end() || !hashIt->is_string() || hashIt->get_ref<const std::string&>().size() != 64 ||
					!IsHex(hashIt->get_ref<const std::string&>()) || expiryIt == resume->end() || !ReadNonNegativeDouble(*expiryIt)) {
					throw std::invalid_argument("Durable profile has an invalid resume credential.");
				}
			}
			if (const auto playerState = profile.find("playerState"); playerState != profile.end()) {
				if (!playerState->is_object()) {
					throw std::invalid_argument("Durable playerState must be an object.");
				}
				for (const char* forbidden : { "actionEvents", "playerId", "serverTime", "sequence", "authorityEpoch" }) {
					if (playerState->contains(forbidden)) {
						throw std::invalid_argument("Durable playerState contains transient state.");
					}
				}
			}
			if (const auto secret = profile.find("profileSecret"); secret != profile.end()) {
				if (!secret->is_string() || secret->get_ref<const std::string&>().size() != ProfileSecretLength ||
					!IsHex(secret->get_ref<const std::string&>())) {
					throw std::invalid_argument("Durable profile has an invalid profile secret.");
				}
			}
			if (const auto attestations = profile.find("saveAttestations"); attestations != profile.end()) {
				if (!attestations->is_array() || attestations->size() > MaximumSaveAttestationsPerProfile) {
					throw std::invalid_argument("Durable save attestations must be a bounded array.");
				}
				for (const auto& record : *attestations) {
					if (!record.is_object()) {
						throw std::invalid_argument("Durable save attestation must be an object.");
					}
					const auto saveFile = record.find("saveFile");
					const auto sha256 = record.find("sha256");
					if (saveFile == record.end() || !saveFile->is_string() ||
						saveFile->get_ref<const std::string&>().empty() ||
						saveFile->get_ref<const std::string&>().size() > 256 ||
						sha256 == record.end() || !sha256->is_string() ||
						sha256->get_ref<const std::string&>().size() != 64 ||
						!IsHex(sha256->get_ref<const std::string&>())) {
						throw std::invalid_argument("Durable profile has an invalid save attestation.");
					}
					if (const auto timestamp = record.find("attestedAt"); timestamp != record.end() && !ReadNonNegativeDouble(*timestamp)) {
						throw std::invalid_argument("Durable profile has an invalid save attestation timestamp.");
					}
					if (const auto slot = record.find("slot"); slot != record.end() && !ReadUInt32(*slot)) {
						throw std::invalid_argument("Durable profile has an invalid save attestation slot.");
					}
				}
			}
		}
		F4T::Drops::DropLedger::Validate(a_root);
		F4T::DeathIntent::Validate(a_root);
		F4T::DeathBag::Validate(a_root);
		F4T::DeathBag::ValidateRecipientInstances(a_root);
	}

	std::optional<F4T::Identity::PersistedResumeCredential> GetResumeCredential(
		const nlohmann::json& a_root,
		std::string_view a_profileId)
	{
		const auto* profile = Profile(a_root, a_profileId);
		if (profile == nullptr) {
			return std::nullopt;
		}
		const auto resume = profile->find("resume");
		if (resume == profile->end()) {
			return std::nullopt;
		}
		if (!resume->is_object()) {
			throw std::invalid_argument("Invalid persisted resume record.");
		}
		const auto hashIt = resume->find("tokenHash");
		const auto expiryIt = resume->find("expiresAt");
		if (hashIt == resume->end() || !hashIt->is_string() || expiryIt == resume->end()) {
			throw std::invalid_argument("Invalid persisted resume record.");
		}
		const auto expiresAt = ReadNonNegativeDouble(*expiryIt);
		if (!expiresAt) {
			throw std::invalid_argument("Invalid persisted resume expiry.");
		}
		std::optional<F4T::Identity::TokenHash> previous;
		if (const auto previousIt = resume->find("previousTokenHash"); previousIt != resume->end() && !previousIt->is_null()) {
			if (!previousIt->is_string()) {
				throw std::invalid_argument("Invalid persisted resume record.");
			}
			previous = HexToHash(previousIt->get_ref<const std::string&>());
		}
		bool confirmed = true;
		if (const auto confirmedIt = resume->find("confirmed"); confirmedIt != resume->end()) {
			if (!confirmedIt->is_boolean()) {
				throw std::invalid_argument("Invalid persisted resume record.");
			}
			confirmed = confirmedIt->get<bool>();
		}
		return F4T::Identity::PersistedResumeCredential{
			HexToHash(hashIt->get_ref<const std::string&>()),
			*expiresAt,
			previous,
			confirmed
		};
	}

	void SaveResumeCredential(
		nlohmann::json& a_root,
		std::string_view a_profileId,
		const F4T::Identity::TokenHash& a_tokenHash,
		const std::optional<F4T::Identity::TokenHash>& a_previousTokenHash,
		bool a_confirmed,
		double a_expiresAt,
		double a_updatedAt)
	{
		if (!std::isfinite(a_expiresAt) || a_expiresAt <= 0.0 || !std::isfinite(a_updatedAt)) {
			throw std::invalid_argument("Resume credential requires a positive finite expiry.");
		}
		auto* profile = Profile(a_root, a_profileId, true, a_updatedAt);
		Json resume{
			{ "tokenHash", HashToHex(a_tokenHash) },
			{ "expiresAt", a_expiresAt }
		};
		if (a_previousTokenHash) {
			resume["previousTokenHash"] = HashToHex(*a_previousTokenHash);
		}
		resume["confirmed"] = a_confirmed;
		(*profile)["resume"] = std::move(resume);
		(*profile)["updatedAt"] = a_updatedAt;
	}

	bool DeleteResumeCredential(
		nlohmann::json& a_root,
		std::string_view a_profileId,
		double a_updatedAt)
	{
		auto* profile = Profile(a_root, a_profileId, false, a_updatedAt);
		if (profile == nullptr || profile->erase("resume") == 0) {
			return false;
		}
		(*profile)["updatedAt"] = a_updatedAt;
		return true;
	}

	std::optional<nlohmann::json> GetPlayerState(const nlohmann::json& a_root, std::string_view a_profileId)
	{
		const auto* profile = Profile(a_root, a_profileId);
		if (profile == nullptr) {
			return std::nullopt;
		}
		const auto stored = profile->find("playerState");
		if (stored == profile->end() || !stored->is_object()) {
			return std::nullopt;
		}
		Json packet{ { "type", "playerState" } };
		for (const char* field : F4T::PlayerStateValidation::kDurablePlayerStateFields) {
			if (const auto found = stored->find(field); found != stored->end()) {
				packet[field] = *found;
			}
		}
		return packet.size() > 1 ? std::optional{ std::move(packet) } : std::nullopt;
	}

	bool CarriesDurablePlayerState(const nlohmann::json& a_playerState)
	{
		const auto normalized = F4T::PlayerStateValidation::NormalizePlayerState(a_playerState);
		if (!normalized) {
			return false;
		}
		for (const char* field : F4T::PlayerStateValidation::kDurablePlayerStateFields) {
			if (normalized->contains(field)) {
				return true;
			}
		}
		return false;
	}

	bool ProgressionRefused(
		const nlohmann::json& a_root,
		std::string_view a_profileId,
		const nlohmann::json& a_playerState,
		double a_nowSeconds)
	{
		auto normalized = F4T::PlayerStateValidation::NormalizePlayerState(a_playerState);
		if (!normalized || !normalized->contains("progression")) {
			return false;
		}
		DropRejectedProgression(*normalized, a_root, a_profileId, a_nowSeconds);
		return !normalized->contains("progression");
	}

	PlayerStateMutationOutcome InspectPlayerStateUpdate(
		const nlohmann::json& a_root,
		std::string_view a_profileId,
		const nlohmann::json& a_playerState,
		double a_nowSeconds)
	{
		auto normalized = F4T::PlayerStateValidation::NormalizePlayerState(a_playerState);
		if (!normalized) {
			return PlayerStateMutationOutcome::NoDurableData;
		}
		DropRejectedProgression(*normalized, a_root, a_profileId, a_nowSeconds);
		bool hasDurable = false;
		for (const char* field : F4T::PlayerStateValidation::kDurablePlayerStateFields) {
			hasDurable = hasDurable || normalized->contains(field);
		}
		if (!hasDurable) {
			return PlayerStateMutationOutcome::NoDurableData;
		}
		const auto* existingProfile = Profile(a_root, a_profileId);
		if (existingProfile == nullptr || !existingProfile->contains("playerState") || !(*existingProfile)["playerState"].is_object()) {
			return PlayerStateMutationOutcome::Changed;
		}
		const auto& existingState = (*existingProfile)["playerState"];
		for (const char* field : F4T::PlayerStateValidation::kDurablePlayerStateFields) {
			const auto found = normalized->find(field);
			if (found != normalized->end()) {
				const auto existingFound = existingState.find(field);
				if (existingFound == existingState.end() || *existingFound != *found) {
					return PlayerStateMutationOutcome::Changed;
				}
			}
		}
		return PlayerStateMutationOutcome::Unchanged;
	}

	PlayerStateMutationOutcome UpdatePlayerState(
		nlohmann::json& a_root,
		std::string_view a_profileId,
		const nlohmann::json& a_playerState,
		double a_updatedAt)
	{
		auto normalized = F4T::PlayerStateValidation::NormalizePlayerState(a_playerState);
		if (!normalized) {
			return PlayerStateMutationOutcome::NoDurableData;
		}
		DropRejectedProgression(*normalized, a_root, a_profileId, a_updatedAt);
		bool hasDurable = false;
		for (const char* field : F4T::PlayerStateValidation::kDurablePlayerStateFields) {
			hasDurable = hasDurable || normalized->contains(field);
		}
		if (!hasDurable) {
			return PlayerStateMutationOutcome::NoDurableData;
		}
		const auto* existingProfile = Profile(a_root, a_profileId);
		if (existingProfile != nullptr && existingProfile->contains("playerState") && (*existingProfile)["playerState"].is_object()) {
			const auto& existingState = (*existingProfile)["playerState"];
			bool anyDifferent = false;
			for (const char* field : F4T::PlayerStateValidation::kDurablePlayerStateFields) {
				const auto found = normalized->find(field);
				if (found != normalized->end()) {
					const auto existingFound = existingState.find(field);
					if (existingFound == existingState.end() || *existingFound != *found) {
						anyDifferent = true;
						break;
					}
				}
			}
			if (!anyDifferent) {
				return PlayerStateMutationOutcome::Unchanged;
			}
		}
		auto* profile = Profile(a_root, a_profileId, true, a_updatedAt);
		Json stored = profile->contains("playerState") && (*profile)["playerState"].is_object()
			? (*profile)["playerState"]
			: Json::object();
		if (const auto progression = normalized->find("progression"); progression != normalized->end() && F4T::ProgressionValidation::RestartsLevelAllowance(stored, *progression)) {
			stored["progressionUpdatedAt"] = a_updatedAt;
		}
		for (const char* field : F4T::PlayerStateValidation::kDurablePlayerStateFields) {
			if (const auto found = normalized->find(field); found != normalized->end()) {
				stored[field] = *found;
			}
		}
		stored["updatedAt"] = a_updatedAt;
		(*profile)["playerState"] = std::move(stored);
		(*profile)["updatedAt"] = a_updatedAt;
		return PlayerStateMutationOutcome::Changed;
	}

	std::optional<std::string> GetProfileSecret(const nlohmann::json& a_root, std::string_view a_profileId)
	{
		const auto* profile = Profile(a_root, a_profileId);
		if (profile == nullptr) {
			return std::nullopt;
		}
		const auto stored = profile->find("profileSecret");
		if (stored == profile->end() || !stored->is_string()) {
			return std::nullopt;
		}
		return stored->get<std::string>();
	}

	bool EnsureProfileSecret(
		nlohmann::json& a_root,
		std::string_view a_profileId,
		std::string_view a_secret,
		double a_updatedAt)
	{
		if (a_secret.size() != ProfileSecretLength || !IsHex(a_secret)) {
			throw std::invalid_argument("A profile secret must be 32 hex characters.");
		}
		if (const auto existing = GetProfileSecret(a_root, a_profileId); existing.has_value()) {
			return false;
		}
		auto* profile = Profile(a_root, a_profileId, true, a_updatedAt);
		(*profile)["profileSecret"] = std::string{ a_secret };
		(*profile)["updatedAt"] = a_updatedAt;
		return true;
	}

	std::vector<F4T::SaveProfileLogic::AttestedSave> GetSaveAttestations(
		const nlohmann::json& a_root,
		std::string_view a_profileId)
	{
		std::vector<F4T::SaveProfileLogic::AttestedSave> result;
		const auto* profile = Profile(a_root, a_profileId);
		if (profile == nullptr) {
			return result;
		}
		const auto records = profile->find("saveAttestations");
		if (records == profile->end()) {
			return result;
		}
		if (!records->is_array()) {
			throw std::invalid_argument("Invalid persisted save attestations.");
		}
		result.reserve(records->size());
		for (const auto& record : *records) {
			const auto saveFile = record.find("saveFile");
			const auto sha256 = record.find("sha256");
			if (!record.is_object() || saveFile == record.end() || !saveFile->is_string() ||
				sha256 == record.end() || !sha256->is_string()) {
				throw std::invalid_argument("Invalid persisted save attestation.");
			}
			std::uint64_t attestedAt = 0;
			if (const auto timestamp = record.find("attestedAt"); timestamp != record.end()) {
				const auto value = ReadNonNegativeDouble(*timestamp);
				if (!value) {
					throw std::invalid_argument("Invalid persisted save attestation timestamp.");
				}
				attestedAt = static_cast<std::uint64_t>(*value);
			}
			result.push_back(F4T::SaveProfileLogic::AttestedSave{
				saveFile->get<std::string>(),
				sha256->get<std::string>(),
				attestedAt,
				ReadAttestationSlot(record)
			});
		}
		return result;
	}

	void ValidateSaveAttestation(const F4T::SaveProfileLogic::AttestedSave& a_save)
	{
		if (a_save.saveFile.empty() || a_save.saveFile.size() > 256 ||
			a_save.sha256Hex.size() != 64 || !IsHex(a_save.sha256Hex)) {
			throw std::invalid_argument("Save attestation requires a save name and a 64-character SHA-256 hash.");
		}
	}

	std::size_t SaveAttestationEvictionIndex(std::span<const std::optional<std::uint32_t>> a_slots)
	{
		for (std::size_t index = 0; index < a_slots.size(); ++index) {
			if (std::count(a_slots.begin(), a_slots.end(), a_slots[index]) > 1) return index;
		}
		return 0;
	}

	void RecordSaveAttestation(
		nlohmann::json& a_root,
		std::string_view a_profileId,
		const F4T::SaveProfileLogic::AttestedSave& a_save,
		double a_updatedAt)
	{
		ValidateSaveAttestation(a_save);
		auto* profile = Profile(a_root, a_profileId, true, a_updatedAt);
		Json records = Json::array();
		if (const auto existing = profile->find("saveAttestations"); existing != profile->end()) {
			if (!existing->is_array()) {
				throw std::invalid_argument("Invalid persisted save attestations.");
			}
			records = *existing;
		}

		auto slot = a_save.slot;
		Json updated = Json::array();
		for (auto& record : records) {
			const auto saveFile = record.is_object() ? record.find("saveFile") : record.end();
			if (saveFile != record.end() && saveFile->is_string() &&
				F4T::SaveProfileLogic::EqualsIgnoreCase(saveFile->get_ref<const std::string&>(), a_save.saveFile)) {
				if (ReadAttestationSlot(record) != a_save.slot) slot = std::nullopt;
				continue;
			}
			updated.push_back(std::move(record));
		}
		Json entry{
			{ "saveFile", a_save.saveFile },
			{ "sha256", a_save.sha256Hex },
			{ "attestedAt", static_cast<double>(a_save.attestedAtUnix) }
		};
		if (slot) entry["slot"] = *slot;
		updated.push_back(std::move(entry));
		while (updated.size() > MaximumSaveAttestationsPerProfile) {
			std::vector<std::optional<std::uint32_t>> slots;
			slots.reserve(updated.size());
			for (const auto& record : updated) slots.push_back(ReadAttestationSlot(record));
			updated.erase(updated.begin() + static_cast<std::ptrdiff_t>(SaveAttestationEvictionIndex(slots)));
		}
		(*profile)["saveAttestations"] = std::move(updated);
		(*profile)["updatedAt"] = a_updatedAt;
	}

	nlohmann::json GetWorldState(const nlohmann::json& a_root)
	{
		const auto world = a_root.find("worldState");
		if (world == a_root.end() || !world->is_object()) {
			throw std::invalid_argument("Durable state worldState must be an object.");
		}
		return *world;
	}

	void UpdateWorldState(nlohmann::json& a_root, const nlohmann::json& a_worldState)
	{
		if (!a_worldState.is_object()) {
			return;
		}
		auto world = a_root.find("worldState");
		if (world == a_root.end() || !world->is_object()) {
			throw std::invalid_argument("Durable state worldState must be an object.");
		}
		for (const char* field : { "timeHHmm", "weatherConsoleArg", "weatherFormId" }) {
			if (const auto found = a_worldState.find(field); found != a_worldState.end()) {
				if (found->is_null()) {
					world->erase(field);
				} else {
					(*world)[field] = *found;
				}
			}
		}
	}

	inline constexpr const char* kDurableStateSealTag = "Pw3Gv6Rk0Tn8Yl2Ja5Dz7Qs";
	static_assert(kDurableStateSealTag != nullptr);
}
