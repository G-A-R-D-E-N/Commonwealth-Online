#include "PersistenceGate.h"

#include "F4TCharacterSlots.h"
#include "ServerOutboundFrames.h"
#include "F4TInventoryContract.h"
#include "F4TPlayerStateValidation.h"

#include <nlohmann/json.hpp>

#include <iostream>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>

namespace CommonwealthOnline::Server
{
	namespace
	{
		using Json = nlohmann::json;

		bool DumpsCleanly(const Json& a_state)
		{
			try {
				(void)a_state.dump();
				return true;
			} catch (const std::exception&) {
				return false;
			}
		}

		std::optional<Json> ScrubbedState(const Json& a_state)
		{
			try {
				return Json::parse(a_state.dump(-1, ' ', false, Json::error_handler_t::replace));
			} catch (const std::exception&) {
				return std::nullopt;
			}
		}

		std::optional<Json> DecodeObject(std::span<const std::uint8_t> a_payload)
		{
			if (a_payload.empty()) {
				return std::nullopt;
			}
			try {
				const auto text = std::string{
					reinterpret_cast<const char*>(a_payload.data()),
					a_payload.size()
				};
				auto packet = Json::parse(text);
				return packet.is_object() ? std::optional{ std::move(packet) } : std::nullopt;
			} catch (...) {
				return std::nullopt;
			}
		}

		std::optional<std::uint32_t> ReadPlayerId(const Json& a_packet)
		{
			const auto found = a_packet.find("playerId");
			if (found == a_packet.end() || found->is_boolean()) {
				return std::nullopt;
			}
			try {
				if (found->is_number_unsigned()) {
					const auto value = found->get<std::uint64_t>();
					if (value >= 1 && value <= (std::numeric_limits<std::uint32_t>::max)()) {
						return static_cast<std::uint32_t>(value);
					}
				}
				if (found->is_number_integer()) {
					const auto value = found->get<std::int64_t>();
					if (value >= 1 && static_cast<std::uint64_t>(value) <= (std::numeric_limits<std::uint32_t>::max)()) {
						return static_cast<std::uint32_t>(value);
					}
				}
			} catch (...) {
			}
			return std::nullopt;
		}

		std::optional<Json> DurablePlayerStatePatch(const Json& a_playerState)
		{
			const auto normalized = F4T::PlayerStateValidation::NormalizePlayerState(a_playerState);
			if (!normalized) {
				return std::nullopt;
			}
			Json durable{ { "type", "playerState" } };
			for (const char* field : F4T::PlayerStateValidation::kDurablePlayerStateFields) {
				if (const auto found = normalized->find(field); found != normalized->end()) {
					durable[field] = *found;
				}
			}
			return durable.size() > 1 ? std::optional{ std::move(durable) } : std::nullopt;
		}

		void MergeDurablePatch(Json& a_target, const Json& a_patch, bool a_overwrite)
		{
			if (!a_target.is_object()) {
				a_target = Json{ { "type", "playerState" } };
			}
			for (const char* field : F4T::PlayerStateValidation::kDurablePlayerStateFields) {
				if (const auto found = a_patch.find(field); found != a_patch.end() &&
					(a_overwrite || !a_target.contains(field))) {
					a_target[field] = *found;
				}
			}
		}

		[[nodiscard]] std::string ReadyPersistenceProfile(const Json& a_packet)
		{
			const auto identity = a_packet.at("profileId").get<std::string>();
			const auto found = a_packet.find("persistenceProfile");
			if (found == a_packet.end() || !found->is_string() || found->get_ref<const std::string&>().empty()) {
				return identity;
			}
			const auto parsed = F4T::CharacterSlots::ParseSlotKey(found->get_ref<const std::string&>());
			if (parsed && parsed->first == identity) {
				return found->get<std::string>();
			}
			return identity;
		}

		[[nodiscard]] bool SlotReadyUnbound(const Json& a_packet)
		{
			const auto slots = a_packet.find("characterSlots");
			if (slots == a_packet.end() || !slots->is_object()) {
				return false;
			}
			const auto bound = slots->find("bound");
			return bound != slots->end() && bound->is_boolean() && !bound->get<bool>();
		}
	}

	namespace
	{
		std::vector<nlohmann::json> StartingGrantPackets(const std::string& a_profileId, const std::optional<nlohmann::json>& a_grant)
		{
			std::vector<nlohmann::json> packets;
			if (!a_grant) return packets;
			const auto& items = a_grant->at("items");
			const auto perPart = F4T::DurableState::kStartingGrantPartItems;
			const auto parts = (items.size() + perPart - 1) / perPart;
			for (std::size_t part = 0; part < parts; ++part) {
				auto slice = Json::array();
				for (std::size_t index = part * perPart; index < items.size() && index < (part + 1) * perPart; ++index) {
					slice.push_back(items[index]);
				}
				packets.push_back(Json{ { "type", "startingInventoryGrant" }, { "profileId", a_profileId }, { "grantId", a_grant->at("grantId") },
					{ "part", part }, { "parts", parts }, { "items", slice } });
			}
			return packets;
		}
	}

	PersistenceGate::PersistenceGate(DurableStateFileStore& a_store, std::optional<nlohmann::json> a_startingInventory) :
		_store(a_store), _startingInventory(a_startingInventory.value_or(nlohmann::json::array()))
	{
		(void)F4T::DurableState::NormalizeStartingInventory(_startingInventory);
	}

	bool PersistenceGate::PrepareSessionReady(OutboundFrame& a_frame, std::string& a_error)
	{
		a_error.clear();
		if (!SavingEnabled()) return true;
		auto packet = DecodeObject(a_frame.payload);
		if (!packet || packet->value("type", std::string{}) != "sessionReady" || !ReadPlayerId(*packet) ||
			!packet->contains("profileId") || !packet->at("profileId").is_string() ||
			packet->at("profileId").get_ref<const std::string&>().empty()) {
			a_error = "sessionReady frame has no profile to attach an inventory to";
			return false;
		}
		const auto profileId = ReadyPersistenceProfile(*packet);
		{
			const std::scoped_lock lock(_gate);
			_inventoryTransfers.erase(a_frame.connectionId);
			_startingGrants.erase(a_frame.connectionId);
			_inventoryVerified.erase(a_frame.connectionId);
		}
		if (SlotReadyUnbound(*packet)) {
			return true;
		}
		const auto existing = _store.GetInventory(profileId);
		if (_store.InventoryAuthorityBlocked() && !existing) return true;
		nlohmann::json inventory;
		if (existing) {
			inventory = *existing;
		} else {
			inventory = nlohmann::json{
				{ "initialized", true }, { "revision", 0u },
				{ "items", F4T::DurableState::NormalizeStartingInventory(_startingInventory) }
			};
		}
		std::string transferError;
		const auto chunks = F4T::InventoryContract::BuildInventoryTransferChunks(inventory, transferError);
		if (!chunks) {
			a_error = "inventory transfer for profile '" + profileId + "' cannot be sent: " + transferError;
			return false;
		}
		(*packet)["serverInventory"] = nlohmann::json{
			{ "initialized", true },
			{ "revision", chunks->front().at("revision") },
			{ "itemCount", chunks->front().at("itemCount") },
			{ "chunkCount", static_cast<std::uint32_t>(chunks->size()) }
		};
		if (packet->dump().size() > 16 * 1024) {
			a_error = "sessionReady frame for profile '" + profileId + "' exceeds the 16 KiB readiness limit";
			return false;
		}
		if (!existing) {
			(void)_store.InitializeInventory(profileId, _startingInventory);
		}
		std::vector<Json> grantPackets;
		if (const auto grant = _store.PendingStartingGrant(profileId)) {
			grantPackets.push_back(Json{ { "type", "startingInventoryRecovery" }, { "profileId", profileId },
				{ "grantId", grant->at("grantId") }, { "state", grant->at("state") } });
		} else if (const auto completed = _store.GetStartingGrant(profileId); completed && completed->contains("recovery")) {
			const auto& recovery = completed->at("recovery");
			Json notice{ { "type", "startingInventoryRecovery" }, { "profileId", profileId },
				{ "grantId", std::string{ F4T::DurableState::kStartingGrantId } }, { "state", recovery.value("consumed", false) ? "consumed" : "resolved" } };
			if (!recovery.value("consumed", false)) {
				notice["saveFile"] = recovery.at("saveFile");
				notice["sha256"] = recovery.at("sha256");
				notice["characterId"] = recovery.at("characterId");
			}
			grantPackets.push_back(std::move(notice));
		}
		(*packet)["startingInventoryRecovery"] = grantPackets.empty() ? Json(nullptr) : grantPackets.front();
		const auto text = packet->dump();
		if (text.size() > 16 * 1024) {
			a_error = "sessionReady frame for profile '" + profileId + "' exceeds the 16 KiB readiness limit";
			return false;
		}
		{
			const std::scoped_lock lock(_gate);
			_inventoryTransfers[a_frame.connectionId] = *chunks;
			_startingGrants[a_frame.connectionId] = std::move(grantPackets);
		}
		a_frame.payload.assign(text.begin(), text.end());
		return true;
	}

	std::vector<OutboundFrame> PersistenceGate::TakeInventoryTransferFrames(std::uint32_t a_connectionId)
	{
		std::vector<nlohmann::json> chunks;
		{
			const std::scoped_lock lock(_gate);
			const auto found = _inventoryTransfers.find(a_connectionId);
			if (found == _inventoryTransfers.end()) return {};
			chunks = std::move(found->second);
			_inventoryTransfers.erase(found);
		}
		std::vector<OutboundFrame> frames;
		frames.reserve(chunks.size());
		for (const auto& chunk : chunks) {
			const auto text = chunk.dump();
			frames.push_back(OutboundFrame{ a_connectionId, Delivery::ReliableOrdered,
				std::vector<std::uint8_t>{ text.begin(), text.end() } });
		}
		return frames;
	}

	std::vector<OutboundFrame> PersistenceGate::TakeStartingGrantFrames(std::uint32_t a_connectionId)
	{
		std::vector<nlohmann::json> packets;
		{
			const std::scoped_lock lock(_gate);
			const auto found = _startingGrants.find(a_connectionId);
			if (found == _startingGrants.end()) return {};
			packets = std::move(found->second);
			_startingGrants.erase(found);
		}
		std::vector<OutboundFrame> frames;
		frames.reserve(packets.size());
		for (const auto& packet : packets) {
			const auto text = packet.dump();
			frames.push_back(OutboundFrame{ a_connectionId, Delivery::ReliableOrdered,
				std::vector<std::uint8_t>{ text.begin(), text.end() } });
		}
		return frames;
	}

	std::vector<OutboundFrame> PersistenceGate::BeginStartingGrant(std::uint32_t a_connectionId, std::string_view a_profileId,
		std::string_view a_characterId, const F4T::SaveProfileLogic::AttestedSave& a_baseline)
	{
		const std::scoped_lock lock(_gate);
		const auto profile = _profilesByConnection.find(a_connectionId);
		if (profile == _profilesByConnection.end() || profile->second != a_profileId ||
			!_store.BeginStartingGrant(profile->second, a_characterId, a_baseline)) return {};
		std::vector<OutboundFrame> frames;
		for (const auto& packet : StartingGrantPackets(profile->second, _store.PendingStartingGrant(profile->second))) {
			const auto text = packet.dump();
			frames.push_back(OutboundFrame{ a_connectionId, Delivery::ReliableOrdered,
				std::vector<std::uint8_t>{ text.begin(), text.end() } });
		}
		return frames;
	}

	bool PersistenceGate::VerifyStartingGrantSave(std::uint32_t a_connectionId, const F4T::SaveProfileLogic::AttestedSave& a_save)
	{
		const std::scoped_lock lock(_gate);
		try { F4T::DurableState::ValidateSaveAttestation(a_save); } catch (...) { _inventoryVerified.erase(a_connectionId); throw; }
		const auto profile = _profilesByConnection.find(a_connectionId);
		if (profile == _profilesByConnection.end()) return false;
		const auto grant = _store.GetStartingGrant(profile->second);
		if (!grant || !grant->contains("recovery")) { _inventoryVerified.erase(a_connectionId); return false; }
		const auto& recovery = grant->at("recovery");
		if (recovery.value("state", std::string{}) != "resolved") { _inventoryVerified.erase(a_connectionId); return false; }
		const F4T::SaveProfileLogic::AttestedSave resolved{ recovery.at("saveFile").get<std::string>(), recovery.at("sha256").get<std::string>(), 0 };
		if (!F4T::SaveProfileLogic::SameAttestedSave(resolved, a_save)) { _inventoryVerified.erase(a_connectionId); return false; }
		if (_inventoryVerified.contains(a_connectionId)) return true;
		if (!_store.ConsumeStartingGrantApproval(profile->second, a_save)) return false;
		_inventoryVerified.insert(a_connectionId);
		return true;
	}
	void PersistenceGate::InvalidateStartingGrantSave(std::uint32_t a_connectionId)
	{
		const std::scoped_lock lock(_gate);
		_inventoryVerified.erase(a_connectionId);
	}

	bool PersistenceGate::InventorySaveReady(std::uint32_t a_connectionId) const
	{
		const std::scoped_lock lock(_gate);
		return InventorySaveReadyLocked(a_connectionId);
	}

	bool PersistenceGate::InventorySaveReadyLocked(std::uint32_t a_connectionId) const
	{
		const auto profile = _profilesByConnection.find(a_connectionId);
		if (profile == _profilesByConnection.end()) return false;
		return !_store.GetStartingGrant(profile->second) || _inventoryVerified.contains(a_connectionId);
	}

	std::optional<F4T::Trade::CommitResult> PersistenceGate::TransferTrade(std::uint32_t a_first, std::uint32_t a_second,
		const F4T::Trade::CommitRequest& a_request)
	{
		const std::scoped_lock lock(_gate);
		if (!InventorySaveReadyLocked(a_first) || !InventorySaveReadyLocked(a_second) ||
			_profilesByConnection.at(a_first) != a_request.first.profileId ||
			_profilesByConnection.at(a_second) != a_request.second.profileId) return std::nullopt;
		return _store.TransferTrade(a_request);
	}

	std::optional<nlohmann::json> PersistenceGate::CapturePlayerStateCandidate(
		std::uint32_t a_connectionId,
		std::span<const std::uint8_t> a_payload,
		const std::optional<nlohmann::json>* a_parsed) const
	{
		if (!SavingEnabled()) return std::nullopt;
		{
			const std::scoped_lock lock(_gate);
			if (!_profilesByConnection.contains(a_connectionId)) {
				return std::nullopt;
			}
		}
		std::optional<nlohmann::json> ownedPacket;
		if (a_parsed == nullptr) ownedPacket = DecodeObject(a_payload);
		const std::optional<nlohmann::json>& packet = a_parsed != nullptr ? *a_parsed : ownedPacket;
		if (!packet) {
			return std::nullopt;
		}
		const auto type = packet->find("type");
		if (type == packet->end() || !type->is_string() || type->get_ref<const std::string&>() != "playerState") {
			return std::nullopt;
		}
		return packet;
	}

	bool PersistenceGate::QueueAcceptedPlayerState(
		std::uint32_t a_connectionId,
		const nlohmann::json& a_playerState)
	{
		if (!SavingEnabled()) return true;
		const auto durable = DurablePlayerStatePatch(a_playerState);
		if (!durable) {
			return false;
		}

		const std::scoped_lock lock(_gate);
		const auto profile = _profilesByConnection.find(a_connectionId);
		if (profile == _profilesByConnection.end()) {
			return false;
		}
		MergeDurablePatch(_pendingPlayerStates[profile->second], *durable, true);
		return true;
	}

	bool PersistenceGate::FlushPendingPlayerStates(bool a_awaitCommit, bool* a_refusedOut)
	{
		const std::scoped_lock lock(_gate);
		bool refused = false;
		std::vector<std::string> refusedProgression;
		auto changed = _store.UpdatePlayerStates(_pendingPlayerStates, &refused, &refusedProgression);
		for (const auto& profile : refusedProgression) {
			for (const auto& [connectionId, boundProfile] : _profilesByConnection) {
				if (boundProfile == profile) {
					_refusedProgression.insert(connectionId);
				}
			}
		}
		if (refused) {
			changed = RetryPendingPlayerStatesLocked(refused) || changed;
		}
		if (a_refusedOut != nullptr) *a_refusedOut = refused;
		if (!refused) _pendingPlayerStates.clear();
		bool quests = false;
		try {
			quests = FlushQuestsLocked(a_awaitCommit);
		} catch (...) {
			if (a_awaitCommit) _store.WaitForCommits();
			throw;
		}
		if (a_awaitCommit) {
			_store.WaitForCommits();
		}
		return changed || quests;
	}

	std::vector<OutboundFrame> PersistenceGate::TakeProgressionRejections()
	{
		const std::scoped_lock lock(_gate);
		std::vector<OutboundFrame> frames;
		for (const auto connectionId : _refusedProgression) {
			frames.push_back(ReliableFrame(connectionId, Json{ { "type", "progressionRejected" }, { "reason", "refused" } }));
		}
		_refusedProgression.clear();
		return frames;
	}

	bool PersistenceGate::RetryPendingPlayerStatesLocked(bool& a_refusedOut)
	{
		bool accepted = false;
		a_refusedOut = false;
		for (auto entry = _pendingPlayerStates.begin(); entry != _pendingPlayerStates.end();) {
			if (!DumpsCleanly(entry->second)) {
				auto scrubbed = ScrubbedState(entry->second);
				if (!scrubbed) {
					std::cerr << "Dropped a player state that cannot be saved, profile " << entry->first << '\n';
					entry = _pendingPlayerStates.erase(entry);
					continue;
				}
				entry->second = std::move(*scrubbed);
			}
			std::unordered_map<std::string, Json> alone;
			alone.emplace(entry->first, entry->second);
			bool refused = false;
			const auto queued = _store.UpdatePlayerStates(alone, &refused);
			if (refused) {
				a_refusedOut = true;
				++entry;
				continue;
			}
			accepted = queued || accepted;
			entry = _pendingPlayerStates.erase(entry);
		}
		return accepted;
	}

	std::optional<DurableReplayPlan> PersistenceGate::SessionReadyDelivered(
		const OutboundFrame& a_frame,
		double a_unixTimeSeconds)
	{
		const auto packet = DecodeObject(a_frame.payload);
		if (!packet) {
			return std::nullopt;
		}
		const auto type = packet->find("type");
		const auto profile = packet->find("profileId");
		const auto playerId = ReadPlayerId(*packet);
		if (type == packet->end() || !type->is_string() || type->get_ref<const std::string&>() != "sessionReady" ||
			profile == packet->end() || !profile->is_string() || profile->get_ref<const std::string&>().empty() || !playerId) {
			return std::nullopt;
		}

		DurableReplayPlan plan;
		plan.profileId = ReadyPersistenceProfile(*packet);
		plan.playerId = *playerId;
		const bool unbound = SlotReadyUnbound(*packet);
		std::optional<Json> pendingPlayerState;
		{
			const std::scoped_lock lock(_gate);
			if (!unbound) {
				_profilesByConnection[a_frame.connectionId] = plan.profileId;
			}
			if (const auto pending = _pendingPlayerStates.find(plan.profileId); pending != _pendingPlayerStates.end()) {
				pendingPlayerState = pending->second;
			}
			plan.playerState = _store.GetPlayerState(plan.profileId);
			if (pendingPlayerState) {
				Json replay = plan.playerState.value_or(Json{ { "type", "playerState" } });
				MergeDurablePatch(replay, *pendingPlayerState, true);
				plan.playerState = std::move(replay);
			}
		}

		const auto world = _store.GetServerWorldState();
		if (const auto time = world.find("timeHHmm"); time != world.end() && time->is_string()) {
			plan.worldPackets.push_back(Json{
				{ "type", "serverWorldState" },
				{ "timeHHmm", *time },
				{ "serverTime", a_unixTimeSeconds }
			});
		}
		if (const auto form = world.find("weatherFormId"); form != world.end() && form->is_string()) {
			Json update{
				{ "type", "serverWorldState" },
				{ "weatherFormId", *form },
				{ "serverTime", a_unixTimeSeconds }
			};
			plan.worldPackets.push_back(std::move(update));
		}
		return plan;
	}

	void PersistenceGate::ConnectionClosed(std::uint32_t a_connectionId)
	{
		const std::scoped_lock lock(_gate);
		_profilesByConnection.erase(a_connectionId);
		_inventoryVerified.erase(a_connectionId);
		_inventoryTransfers.erase(a_connectionId);
		_startingGrants.erase(a_connectionId);
	}

	void PersistenceGate::RebindConnectionProfile(std::uint32_t a_connectionId, std::string_view a_profileKey)
	{
		const std::scoped_lock lock(_gate);
		_profilesByConnection.insert_or_assign(a_connectionId, std::string{ a_profileKey });
		_inventoryVerified.erase(a_connectionId);
	}

	bool PersistenceGate::ProfileInUseByOtherConnection(std::string_view a_profileKey, std::uint32_t a_exceptConnection) const
	{
		const std::scoped_lock lock(_gate);
		for (const auto& [connectionId, profile] : _profilesByConnection) {
			if (connectionId != a_exceptConnection && profile == a_profileKey) return true;
		}
		return false;
	}

	DurableStateFileStore& PersistenceGate::Store() noexcept
	{
		return _store;
	}

	std::optional<F4T::Drops::TransactionResult> PersistenceGate::ApplyInventoryRequest(std::uint32_t a_connectionId,
		const Json& a_request, const F4T::Drops::InventoryPosition& a_position, bool a_drop)
	{
		const std::scoped_lock lock(_gate);
		if (!InventorySaveReadyLocked(a_connectionId)) return std::nullopt;
		const auto profile = _profilesByConnection.find(a_connectionId);
		if (profile == _profilesByConnection.end()) return std::nullopt;
		return a_drop ? _store.DropInventory(profile->second, a_request, a_position) : _store.PickupInventory(profile->second, a_request, a_position);
	}

	std::optional<F4T::InventorySnapshot::Outcome> PersistenceGate::ReconcileInventory(std::uint32_t a_connectionId,
		const Json& a_request)
	{
		const std::scoped_lock lock(_gate);
		if (!InventorySaveReadyLocked(a_connectionId)) return std::nullopt;
		const auto profile = _profilesByConnection.find(a_connectionId);
		if (profile == _profilesByConnection.end()) return std::nullopt;
		return _store.ReconcileInventory(profile->second, a_request);
	}

	std::optional<F4T::Drops::TransactionResult> PersistenceGate::ApplyContainerRequest(std::uint32_t a_connectionId,
		const Json& a_request, const F4T::Drops::InventoryPosition& a_position)
	{
		const std::scoped_lock lock(_gate);
		if (!InventorySaveReadyLocked(a_connectionId)) return std::nullopt;
		const auto profile = _profilesByConnection.find(a_connectionId);
		if (profile == _profilesByConnection.end()) return std::nullopt;
		return _store.TransferContainer(profile->second, a_request, a_position);
	}

	void PersistenceGate::NoteRefusedContainerMove(std::uint32_t a_connectionId, const Json& a_request, const F4T::Drops::InventoryPosition& a_position, std::string_view a_reason)
	{
		const std::scoped_lock lock(_gate);
		const auto profile = _profilesByConnection.find(a_connectionId);
		if (profile != _profilesByConnection.end() && InventorySaveReadyLocked(a_connectionId)) _store.NoteRefusedContainerMove(profile->second, a_request, a_position, a_reason);
	}

	std::optional<F4T::Replication::SnapshotState> PersistenceGate::GetDropSnapshot() const
	{
		return _store.GetDropSnapshot();
	}

	nlohmann::json PersistenceGate::GetFactions() const
	{
		if (!SavingEnabled()) return nlohmann::json::object();
		return _store.GetFactions();
	}

	nlohmann::json PersistenceGate::ReadStoredFactions() const
	{
		return _store.GetFactions();
	}

	bool PersistenceGate::SaveFactions(const nlohmann::json& a_factions)
	{
		if (!SavingEnabled()) return true;
		return _store.SaveFactions(a_factions);
	}

	nlohmann::json PersistenceGate::ReadStoredKnownPeople() const
	{
		return _store.GetKnownPeople();
	}

	bool PersistenceGate::SaveKnownPeople(const nlohmann::json& a_knownPeople)
	{
		if (!SavingEnabled()) return true;
		return _store.SaveKnownPeople(a_knownPeople);
	}

	nlohmann::json PersistenceGate::ReadStoredSettlementAccess() const
	{
		return _store.GetSettlementAccess();
	}

	bool PersistenceGate::SaveSettlementAccess(const nlohmann::json& a_settlementAccess, std::optional<std::uint64_t> a_eraseChargeOperationId)
	{
		if (!SavingEnabled()) return true;
		return _store.SaveSettlementAccess(a_settlementAccess, a_eraseChargeOperationId);
	}

	nlohmann::json PersistenceGate::GetQuests() const
	{
		return _store.GetQuests();
	}

	bool PersistenceGate::SaveQuests(const nlohmann::json& a_quests)
	{
		const std::scoped_lock lock(_gate);
		if (!_store.SaveQuests(a_quests)) return false;
		_pendingQuests.reset();
		return true;
	}

	void PersistenceGate::QueueQuests(const nlohmann::json& a_quests)
	{
		const std::scoped_lock lock(_gate);
		_pendingQuests = a_quests;
	}

	bool PersistenceGate::FlushQuests(bool a_awaitCommit)
	{
		const std::scoped_lock lock(_gate);
		return FlushQuestsLocked(a_awaitCommit);
	}

	bool PersistenceGate::FlushQuestsLocked(bool a_awaitCommit)
	{
		if (!_pendingQuests) return false;
		const auto quests = std::move(*_pendingQuests);
		_pendingQuests.reset();
		try {
			if (!_store.SaveQuests(quests, a_awaitCommit)) {
				throw std::runtime_error("quest save rejected");
			}
		} catch (...) {
			_pendingQuests = quests;
			throw;
		}
		return true;
	}

	bool PersistenceGate::SaveQuestsBackup(const nlohmann::json& a_quests)
	{
		return _store.SaveQuestsBackup(a_quests);
	}

	nlohmann::json PersistenceGate::GetWorkshop() const
	{
		if (!SavingEnabled()) return nlohmann::json::object();
		return _store.GetWorkshop();
	}

	bool PersistenceGate::SaveWorkshop(const nlohmann::json& a_workshop, bool a_awaitCommit, std::optional<std::uint64_t> a_eraseChargeOperationId)
	{
		if (!SavingEnabled()) return true;
		return _store.SaveWorkshop(a_workshop, a_awaitCommit, a_eraseChargeOperationId);
	}

	nlohmann::json PersistenceGate::ReadStoredParkedSuits() const
	{
		return _store.GetParkedSuits();
	}

	bool PersistenceGate::SaveParkedSuits(const nlohmann::json& a_parkedSuits, bool a_awaitCommit)
	{
		if (!SavingEnabled()) return true;
		return _store.SaveParkedSuits(a_parkedSuits, a_awaitCommit);
	}

	std::optional<std::string> PersistenceGate::ProfileForConnection(std::uint32_t a_connectionId) const
	{
		const std::scoped_lock lock(_gate);
		const auto found = _profilesByConnection.find(a_connectionId);
		return found == _profilesByConnection.end() ? std::nullopt : std::optional{ found->second };
	}
	bool PersistenceGate::SetSavingEnabled(bool a_enabled)
	{
		if (!a_enabled) {
			bool refused = false;
			try {
				(void)FlushPendingPlayerStates(false, &refused);
			} catch (const std::exception& a_error) {
				std::cerr << "Flush before disabling saving failed: " << a_error.what() << std::endl;
				return false;
			}
			if (refused) return false;
		}
		const std::scoped_lock lock(_gate);
		_savingEnabled = a_enabled;
		if (!a_enabled) {
			_pendingPlayerStates.clear();
		}
		return true;
	}

	bool PersistenceGate::SavingEnabled() const
	{
		const std::scoped_lock lock(_gate);
		return _savingEnabled;
	}
}
