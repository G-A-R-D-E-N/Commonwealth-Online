#include "pch.h"

#include "F4TInventoryGrant.h"
#include "F4TInventoryGrantDelivery.h"

#include "F4TInventoryContract.h"
#include "F4TInventoryDropGate.h"
#include "F4TInventorySnapshot.h"
#include "F4TInventorySync.h"
#include "F4TContentManifest.h"
#include "F4TNetworking.h"
#include "F4TNetworkingInternal.h"
#include "F4TDurableState.h"
#include "F4TSaveLineageSession.h"
#include "F4TSaveProfileLogic.h"
#include "F4TSessionLaunch.h"
#include "F4TSessionWorldState.h"
#include "F4TUIClient.h"

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <format>
#include <limits>
#include <mutex>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

namespace F4T::InventoryGrant
{
	using namespace F4T::Networking::Detail;

	namespace
	{
		struct Assembly
		{
			std::string profileId;
			std::string grantId;
			std::size_t parts{};
			std::size_t nextPart{};
			std::uint64_t generation{};
			std::uint64_t loadEpoch{};
			nlohmann::json items = nlohmann::json::array();
		};

		struct LoadedSave
		{
			std::string name;
			std::string hash;
			std::uint64_t generation{};
			std::uint64_t epoch{};
		};

		struct Recovery
		{
			std::string profile;
			std::string grant;
			std::string state;
			std::uint64_t generation{};
			std::uint64_t requestedEpoch{};
			bool requested{};
			bool consumed{};
			bool notified{};
			std::chrono::steady_clock::time_point retryAt{};
			std::chrono::steady_clock::time_point seenAt{ std::chrono::steady_clock::now() };
			std::string approvedSave;
			std::string approvedHash;
			std::string approvedCharacter;
		};

		std::mutex g_mutex;
		std::optional<Assembly> g_assembly;
		std::optional<LoadedSave> g_loading;
		std::optional<LoadedSave> g_loaded;
		std::optional<Recovery> g_recovery;
		std::uint64_t g_loadEpoch{};
		bool g_unattestedLoad{};
		std::uint64_t g_recoveryReadyGeneration{};
		bool g_protectedSession{};
		std::uint64_t g_protectedGeneration{};

		// RE address 0x401b90 (OG 1.10.163) for TESObjectREFR::AddObjecttoContainer; source:ResearchEvidence/function-corpus/by-subsystem/1.10.163/Forms_TES.part-004.txt
		bool AddItems(RE::PlayerCharacter& a_player, const nlohmann::json& a_items)
		{
			const bool applied = F4T::InventoryGrantDelivery::Apply(a_items, ResolveItem, [&](RE::TESBoundObject* a_object, std::uint32_t a_count) {
				auto remaining = a_count;
				while (remaining > 0) {
					const auto chunk = (std::min)(remaining, static_cast<std::uint32_t>((std::numeric_limits<std::int32_t>::max)()));
					a_player.AddObjectToContainer(a_object, RE::BSTSmartPointer<RE::ExtraDataList>{}, static_cast<std::int32_t>(chunk), nullptr, RE::ITEM_REMOVE_REASON::kNone);
					remaining -= chunk;
				}
			});
			if (!applied) LogThrottledWarning("inventory_grant_unresolved", "Starting inventory grant could not be resolved; no items were added.");
			return applied;
		}

		void ApplyOnGameThread(const std::string& a_profileId, const std::string& a_grantId, const nlohmann::json& a_items, std::uint64_t a_generation, std::uint64_t a_loadEpoch)
		{
			bool applied = false;
			{
				const std::scoped_lock lock(g_mutex);
				if (!g_recovery || !g_recovery->consumed || g_recovery->state != "applying" || g_recovery->profile != a_profileId || g_recovery->grant != a_grantId ||
					!g_loaded || g_loaded->epoch != a_loadEpoch || g_loadEpoch != a_loadEpoch ||
					F4T::Networking::GetConnectionGeneration() != a_generation || !F4T::Networking::IsConnectedToServer()) return;
				g_recovery->state = "attempted";
				auto* player = RE::PlayerCharacter::GetSingleton();
				const auto character = F4T::SaveProfileLogic::ExtractCharacterId(g_loaded->name);
				if (player && player->GetParentCell() && character && F4T::SaveProfileLogic::CharacterIdsEqual(*character, F4T::SessionLaunch::ActiveCharacterId()) &&
					!F4T::SessionWorldState::IsLoadingScreenActive()) {
					try { applied = AddItems(*player, a_items); } catch (...) { applied = false; }
				}
			}
			F4T::UIClient::ShowStartingInventoryRecoveryNotification(applied ?
				"Starting items were added. Disconnect normally to save, then ask the server administrator to reconcile your save before continuing." :
				"Starting items could not be confirmed. Do not retry them; disconnect and ask the server administrator for recovery.");
		}
	}

	RE::TESBoundObject* ResolveItem(const std::string& a_key)
	{
		const auto parts = F4T::InventoryContract::ParseItemKey(a_key);
		auto* handler = RE::TESDataHandler::GetSingleton();
		if (!parts || !handler) {
			return nullptr;
		}
		std::uint32_t resolvedId{};
		if (const auto* file = handler->LookupLoadedModByName(parts->plugin)) {
			if (file->compileIndex == 0xFF || parts->localId > 0xFFFFFF) {
				return nullptr;
			}
			resolvedId = (static_cast<std::uint32_t>(file->compileIndex) << 24) | parts->localId;
		} else if (const auto* light = handler->LookupLoadedLightModByName(parts->plugin)) {
			if (parts->localId > 0xFFF) {
				return nullptr;
			}
			resolvedId = 0xFE000000 | (static_cast<std::uint32_t>(light->smallFileCompileIndex) << 12) | parts->localId;
		} else {
			return nullptr;
		}
		auto* object = RE::TESForm::GetFormByID<RE::TESBoundObject>(resolvedId);
		if (!object || F4T::InventoryDropGate::TranslateItemKey(object) != a_key) {
			return nullptr;
		}
		return object;
	}

	bool CheckLaunchSave(const std::optional<std::string>& a_save, bool& a_approved)
	{
		a_approved = false;
		std::optional<Recovery> recovery;
		{
			const std::scoped_lock lock(g_mutex);
			if (g_recoveryReadyGeneration != F4T::Networking::GetConnectionGeneration()) return false;
			if (!g_recovery || g_recovery->generation != F4T::Networking::GetConnectionGeneration()) return true;
			recovery = g_recovery;
		}
		if (recovery->state == "unstarted") return true;
		if (recovery->state != "resolved" || !a_save ||
			!F4T::SaveProfileLogic::EqualsIgnoreCase(*a_save, recovery->approvedSave)) return false;
		const auto character = F4T::SaveProfileLogic::ExtractCharacterId(*a_save);
		const auto hash = F4T::SaveLineageSession::HashSaveForRecovery(*a_save);
		if (!character || !hash || !F4T::SaveProfileLogic::CharacterIdsEqual(*character, recovery->approvedCharacter) ||
			!F4T::SaveProfileLogic::EqualsIgnoreCase(*hash, recovery->approvedHash)) return false;
		const std::scoped_lock lock(g_mutex);
		if (!g_recovery || g_recovery->generation != recovery->generation || g_recovery->state != "resolved" ||
			F4T::Networking::GetConnectionGeneration() != recovery->generation) return false;
		a_approved = true;
		return true;
	}
	void OnPreLoad(const char* a_name, std::size_t a_length)
	{
		bool disconnect = false;
		std::uint64_t epoch{};
		std::uint64_t generation{};
		{
			const std::scoped_lock lock(g_mutex);
			disconnect = g_protectedSession && g_protectedGeneration == F4T::Networking::GetConnectionGeneration() && F4T::Networking::IsConnectedToServer();
			g_protectedSession = false;
			g_loading.reset();
			g_loaded.reset();
			g_assembly.reset();
			g_unattestedLoad = true;
			epoch = ++g_loadEpoch;
			generation = F4T::Networking::GetConnectionGeneration();
			if (disconnect) g_recovery.reset();
		}
		if (disconnect) {
			F4T::Networking::DisconnectFromLocalServer();
			return;
		}
		if (!a_name || a_length == 0 || a_length > 256) return;
		const std::string name{ a_name, a_length };
		if (name.find('\0') != std::string::npos || !F4T::SaveProfileLogic::ExtractCharacterId(name)) return;
		const auto hash = F4T::SaveLineageSession::HashSaveForRecovery(name);
		if (!hash) return;
		const std::scoped_lock lock(g_mutex);
		if (g_loadEpoch == epoch && generation == F4T::Networking::GetConnectionGeneration()) g_loading = LoadedSave{ name, *hash, generation, epoch };
	}

	void OnPostLoad(bool a_succeeded)
	{
		std::optional<LoadedSave> loading;
		{
			const std::scoped_lock lock(g_mutex);
			loading = std::move(g_loading);
			g_loading.reset();
		}
		if (!a_succeeded || !loading || loading->generation != F4T::Networking::GetConnectionGeneration()) return;
		const auto hash = F4T::SaveLineageSession::HashSaveForRecovery(loading->name);
		if (!hash || !F4T::SaveProfileLogic::EqualsIgnoreCase(*hash, loading->hash)) return;
		const std::scoped_lock lock(g_mutex);
		if (loading->epoch == g_loadEpoch && loading->generation == F4T::Networking::GetConnectionGeneration()) {
			g_loaded = std::move(loading);
			g_unattestedLoad = false;
		}
	}

	void OnNewGame()
	{
		bool disconnect = false;
		{
			const std::scoped_lock lock(g_mutex);
			disconnect = g_protectedSession && g_protectedGeneration == F4T::Networking::GetConnectionGeneration() && F4T::Networking::IsConnectedToServer();
			g_protectedSession = false;
			g_loading.reset();
			g_loaded.reset();
			g_assembly.reset();
			g_unattestedLoad = false;
			++g_loadEpoch;
			if (disconnect) g_recovery.reset();
		}
		if (disconnect) F4T::Networking::DisconnectFromLocalServer();
	}

	std::optional<std::string> LoadedCharacterId()
	{
		const std::scoped_lock lock(g_mutex);
		if (!g_loaded) {
			return std::nullopt;
		}
		return F4T::SaveProfileLogic::ExtractCharacterId(g_loaded->name);
	}

	std::uint64_t LoadEpoch()
	{
		const std::scoped_lock lock(g_mutex);
		return g_loadEpoch;
	}

	bool UnattestedLoad()
	{
		const std::scoped_lock lock(g_mutex);
		return g_unattestedLoad;
	}

	void NoteRecovery(const nlohmann::json& a_packet)
	{
		const auto profile = a_packet.find("profileId");
		const auto grant = a_packet.find("grantId");
		const auto state = a_packet.find("state");
		if (profile == a_packet.end() || !profile->is_string() || profile->get_ref<const std::string&>().empty() ||
			grant == a_packet.end() || !grant->is_string() || grant->get_ref<const std::string&>() != F4T::DurableState::kStartingGrantId ||
			state == a_packet.end() || !state->is_string()) return;
		const auto value = state->get<std::string>();
		if (value != "unstarted" && value != "intent" && value != "legacy" && value != "consumed" && value != "resolved") return;
		std::string approvedSave;
		std::string approvedHash;
		std::string approvedCharacter;
		if (value == "resolved") {
			const auto save = a_packet.find("saveFile");
			const auto hash = a_packet.find("sha256");
			const auto character = a_packet.find("characterId");
			if (a_packet.size() != 7 || save == a_packet.end() || !save->is_string() || hash == a_packet.end() || !hash->is_string() ||
				character == a_packet.end() || !character->is_string()) return;
			approvedSave = save->get<std::string>();
			approvedHash = hash->get<std::string>();
			approvedCharacter = character->get<std::string>();
			if (!F4T::ContentManifest::IsValidFingerprint(approvedHash) ||
				!F4T::SaveProfileLogic::CanRebindProfileSave(approvedCharacter, approvedSave)) return;
		} else if (a_packet.size() != 4) return;
		const std::scoped_lock lock(g_mutex);
		if (g_recovery && g_recovery->generation == F4T::Networking::GetConnectionGeneration() &&
			(g_recovery->requested || g_recovery->consumed)) return;
		g_recovery = Recovery{ profile->get<std::string>(), grant->get<std::string>(), value, F4T::Networking::GetConnectionGeneration() };
		g_recovery->approvedSave = std::move(approvedSave);
		g_recovery->approvedHash = std::move(approvedHash);
		g_recovery->approvedCharacter = std::move(approvedCharacter);
		g_assembly.reset();
	}

	void NoteRecoveryWelcome(const nlohmann::json& a_packet)
	{
		const auto generation = F4T::Networking::GetConnectionGeneration();
		if (a_packet.is_object()) {
			NoteRecovery(a_packet);
			const std::scoped_lock lock(g_mutex);
			if (g_recovery && g_recovery->generation == generation) g_recoveryReadyGeneration = generation;
		} else if (a_packet.is_null()) {
			const std::scoped_lock lock(g_mutex);
			g_recovery.reset();
			g_recoveryReadyGeneration = generation;
		}
	}
	void NoteVerification(const nlohmann::json& a_packet)
	{
		const auto accepted = a_packet.find("accepted");
		if (a_packet.size() != 2 || accepted == a_packet.end() || !accepted->is_boolean()) return;
		const std::scoped_lock lock(g_mutex);
		if (!g_recovery || g_recovery->state != "resolved" || !g_recovery->requested || !g_loaded ||
			g_recovery->requestedEpoch != g_loadEpoch || g_loaded->epoch != g_loadEpoch ||
			g_recovery->generation != F4T::Networking::GetConnectionGeneration()) return;
		g_recovery->state = accepted->get<bool>() ? "verified" : "refused";
		g_recovery->notified = accepted->get<bool>();
	}

	void Tick()
	{
		std::optional<Recovery> recovery;
		std::optional<LoadedSave> loaded;
		const char* notice = nullptr;
		{
			const std::scoped_lock lock(g_mutex);
			if (!g_recovery || g_recovery->generation != F4T::Networking::GetConnectionGeneration()) return;
			if ((g_recovery->state == "intent" || g_recovery->state == "legacy" || g_recovery->state == "consumed" || g_recovery->state == "refused") && !g_recovery->notified) {
				g_recovery->notified = true;
				notice = "Starting inventory needs recovery. Disconnect and ask the server administrator to inspect your character and save.";
			} else if ((g_recovery->state == "unstarted" || g_recovery->state == "resolved") && !g_recovery->notified &&
				std::chrono::steady_clock::now() - g_recovery->seenAt > std::chrono::seconds{ 30 }) {
				g_recovery->notified = true;
				notice = g_recovery->requested ? "Starting inventory could not be confirmed. Disconnect and ask the server administrator to inspect the grant and reconcile your current save." :
					"Starting inventory needs a loaded multiplayer save. Complete character setup, disconnect normally to save, then reconnect.";
			}
			if ((g_recovery->state != "unstarted" && g_recovery->state != "resolved") || !g_loaded ||
				g_loaded->generation != g_recovery->generation || std::chrono::steady_clock::now() < g_recovery->retryAt) {
				if (!notice) return;
			} else {
				recovery = g_recovery;
				loaded = g_loaded;
			}
		}
		if (notice) F4T::UIClient::ShowStartingInventoryRecoveryNotification(notice);
		if (!recovery || !loaded) return;
		if (!F4T::InventorySync::GetStatus().sessionActive || F4T::SessionWorldState::IsLoadingScreenActive()) return;
		const auto character = F4T::SaveProfileLogic::ExtractCharacterId(loaded->name);
		if (!character || !F4T::SaveProfileLogic::CharacterIdsEqual(*character, F4T::SessionLaunch::ActiveCharacterId())) return;
		if (recovery->state == "resolved" &&
			(!F4T::SaveProfileLogic::CharacterIdsEqual(*character, recovery->approvedCharacter) ||
				!F4T::SaveProfileLogic::EqualsIgnoreCase(loaded->name, recovery->approvedSave) ||
				!F4T::SaveProfileLogic::EqualsIgnoreCase(loaded->hash, recovery->approvedHash))) {
			const std::scoped_lock lock(g_mutex);
			if (g_recovery && g_recovery->generation == recovery->generation && g_loadEpoch == loaded->epoch) { g_recovery->state = "refused"; g_recovery->notified = false; }
			return;
		}
		const auto hash = F4T::SaveLineageSession::HashSaveForRecovery(loaded->name);
		if (!hash || !F4T::SaveProfileLogic::EqualsIgnoreCase(*hash, loaded->hash)) return;
		{
			const std::scoped_lock lock(g_mutex);
			if (!g_recovery || !g_loaded || g_recovery->generation != recovery->generation || g_loaded->epoch != loaded->epoch ||
				g_loadEpoch != loaded->epoch || g_recovery->state != recovery->state) return;
			g_recovery->requested = true;
			g_recovery->requestedEpoch = loaded->epoch;
			g_recovery->retryAt = std::chrono::steady_clock::now() + std::chrono::seconds{ 5 };
			g_protectedSession = true;
			g_protectedGeneration = recovery->generation;
		}
		const auto packet = recovery->state == "unstarted" ?
			nlohmann::json{ { "type", "startingInventoryBegin" }, { "profileId", recovery->profile }, { "grantId", recovery->grant },
				{ "characterId", *character }, { "saveFile", loaded->name }, { "sha256", loaded->hash } } :
			nlohmann::json{ { "type", "startingInventoryVerify" }, { "saveFile", loaded->name }, { "sha256", loaded->hash } };
		if (!SendPacketBytesForGeneration(packet.dump(), recovery->generation)) {
			const std::scoped_lock lock(g_mutex);
			if (g_recovery && g_recovery->generation == recovery->generation && g_recovery->requestedEpoch == loaded->epoch &&
				g_recovery->state == recovery->state) {
				g_recovery->requested = false;
				g_recovery->retryAt = {};
			}
		}
	}

	void NoteStartingGrant(const nlohmann::json& a_grant)
	{
		const auto generation = F4T::Networking::GetConnectionGeneration();
		const auto profile = a_grant.find("profileId");
		const auto grant = a_grant.find("grantId");
		const auto part = a_grant.find("part");
		const auto parts = a_grant.find("parts");
		const auto items = a_grant.find("items");
		if (profile == a_grant.end() || !profile->is_string() || grant == a_grant.end() || !grant->is_string() ||
			part == a_grant.end() || !part->is_number_unsigned() || parts == a_grant.end() || !parts->is_number_unsigned() ||
			items == a_grant.end() || !items->is_array() || items->size() > F4T::DurableState::kStartingGrantPartItems ||
			parts->get<std::size_t>() == 0 || parts->get<std::size_t>() > F4T::DurableState::MaximumStartingInventoryItems / F4T::DurableState::kStartingGrantPartItems ||
			part->get<std::size_t>() >= parts->get<std::size_t>()) {
			LogThrottledWarning("inventory_grant_malformed", "Ignoring a malformed starting inventory grant.");
			return;
		}

		const auto index = part->get<std::size_t>();
		const auto count = parts->get<std::size_t>();
		std::optional<Assembly> complete;
		{
			const std::scoped_lock lock(g_mutex);
			if (!g_recovery || g_recovery->state != "unstarted" || !g_recovery->requested || g_recovery->consumed ||
				g_recovery->generation != generation || g_recovery->requestedEpoch != g_loadEpoch || !g_loaded ||
				g_loaded->epoch != g_loadEpoch || g_recovery->profile != profile->get<std::string>() ||
				g_recovery->grant != grant->get<std::string>()) return;
			if (index == 0) {
				g_assembly = Assembly{ profile->get<std::string>(), grant->get<std::string>(), count, 0, generation, g_loadEpoch, nlohmann::json::array() };
			}
			if (!g_assembly || g_assembly->profileId != profile->get<std::string>() || g_assembly->grantId != grant->get<std::string>() ||
				g_assembly->parts != count || g_assembly->nextPart != index || g_assembly->generation != generation) {
				g_assembly.reset();
				LogThrottledWarning("inventory_grant_order", "Discarded a starting inventory grant that arrived out of order.");
				return;
			}
			for (const auto& item : *items) {
				g_assembly->items.push_back(item);
			}
			g_assembly->nextPart += 1;
			if (g_assembly->nextPart < g_assembly->parts) {
				return;
			}
			complete = std::move(g_assembly);
			g_assembly.reset();
			g_recovery->consumed = true;
			g_recovery->state = "applying";
		}

		const auto normalized = F4T::InventorySnapshot::NormalizeItems(complete->items);
		if (!normalized) {
			LogThrottledWarning("inventory_grant_invalid", "Ignoring a starting inventory grant the server limits refuse.");
			const std::scoped_lock lock(g_mutex);
			if (g_recovery && g_recovery->generation == generation) g_recovery->state = "intent";
			return;
		}
		const auto* taskInterface = F4SE::GetTaskInterface();
		if (!taskInterface) {
			LogThrottledWarning("inventory_grant_task", "Could not queue the starting inventory grant: task interface unavailable.");
			const std::scoped_lock lock(g_mutex);
			if (g_recovery && g_recovery->generation == generation) g_recovery->state = "intent";
			return;
		}
		taskInterface->AddTask([profileId = complete->profileId, grantId = complete->grantId, items = *normalized, generation = complete->generation, loadEpoch = complete->loadEpoch]() {
			ApplyOnGameThread(profileId, grantId, items, generation, loadEpoch);
		});
	}
}
