#include "PersistenceGate.h"

#include "F4TCharacterSlots.h"
#include "F4TInventoryContract.h"
#include "F4TPlayerStateValidation.h"

#include <nlohmann/json.hpp>

#include <limits>
#include <optional>
#include <string>
#include <utility>

namespace CommonwealthOnline::Server
{
	namespace
	{
		using Json = nlohmann::json;

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
			for (const char* field : { "characterName", "appearance", "equippedItems", "powerArmor" }) {
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
			for (const char* field : { "characterName", "appearance", "equippedItems", "powerArmor" }) {
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

	PersistenceGate::PersistenceGate(DurableStateFileStore& a_store, std::optional<nlohmann::json> a_startingInventory) :
		_store(a_store), _startingInventory(std::move(a_startingInventory))
	{
		if (_startingInventory) (void)F4T::DurableState::NormalizeStartingInventory(*_startingInventory);
	}

	bool PersistenceGate::PrepareSessionReady(OutboundFrame& a_frame, std::string& a_error)
	{
		a_error.clear();
		if (!SavingEnabled()) return true;
		if (!_startingInventory) return true;
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
				{ "items", F4T::DurableState::NormalizeStartingInventory(*_startingInventory) }
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
		const auto text = packet->dump();
		if (text.size() > 16 * 1024) {
			a_error = "sessionReady frame for profile '" + profileId + "' exceeds the 16 KiB readiness limit";
			return false;
		}
		if (!existing) {
			(void)_store.InitializeInventory(profileId, *_startingInventory);
		}
		{
			const std::scoped_lock lock(_gate);
			_inventoryTransfers[a_frame.connectionId] = *chunks;
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

	std::optional<nlohmann::json> PersistenceGate::CapturePlayerStateCandidate(
		std::uint32_t a_connectionId,
		std::span<const std::uint8_t> a_payload) const
	{
		if (!SavingEnabled()) return std::nullopt;
		{
			const std::scoped_lock lock(_gate);
			if (!_profilesByConnection.contains(a_connectionId)) {
				return std::nullopt;
			}
		}
		const auto packet = DecodeObject(a_payload);
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

	bool PersistenceGate::FlushPendingPlayerStates(bool a_awaitCommit)
	{
		const std::scoped_lock lock(_gate);
		const auto changed = _store.UpdatePlayerStates(_pendingPlayerStates);
		_pendingPlayerStates.clear();
		const auto quests = FlushQuestsLocked(a_awaitCommit);
		if (a_awaitCommit) {
			_store.WaitForCommits();
		}
		return changed || quests;
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
		_inventoryTransfers.erase(a_connectionId);
	}

	void PersistenceGate::RebindConnectionProfile(std::uint32_t a_connectionId, std::string_view a_profileKey)
	{
		const std::scoped_lock lock(_gate);
		_profilesByConnection.insert_or_assign(a_connectionId, std::string{ a_profileKey });
	}

	DurableStateFileStore& PersistenceGate::Store() noexcept
	{
		return _store;
	}

	std::optional<F4T::Drops::TransactionResult> PersistenceGate::ApplyInventoryRequest(std::uint32_t a_connectionId,
		const Json& a_request, const F4T::Drops::InventoryPosition& a_position, bool a_drop)
	{
		const std::scoped_lock lock(_gate);
		const auto profile = _profilesByConnection.find(a_connectionId);
		if (profile == _profilesByConnection.end()) return std::nullopt;
		return a_drop ? _store.DropInventory(profile->second, a_request, a_position) : _store.PickupInventory(profile->second, a_request, a_position);
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

	void PersistenceGate::SaveFactions(const nlohmann::json& a_factions)
	{
		if (!SavingEnabled()) return;
		_store.SaveFactions(a_factions);
	}

	nlohmann::json PersistenceGate::ReadStoredSettlementAccess() const
	{
		return _store.GetSettlementAccess();
	}

	void PersistenceGate::SaveSettlementAccess(const nlohmann::json& a_settlementAccess)
	{
		if (!SavingEnabled()) return;
		_store.SaveSettlementAccess(a_settlementAccess);
	}

	nlohmann::json PersistenceGate::GetQuests() const
	{
		return _store.GetQuests();
	}

	void PersistenceGate::SaveQuests(const nlohmann::json& a_quests)
	{
		const std::scoped_lock lock(_gate);
		_pendingQuests.reset();
		_store.SaveQuests(a_quests);
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
			_store.SaveQuests(quests, a_awaitCommit);
		} catch (...) {
			_pendingQuests = quests;
			throw;
		}
		return true;
	}

	void PersistenceGate::SaveQuestsBackup(const nlohmann::json& a_quests)
	{
		_store.SaveQuestsBackup(a_quests);
	}

	nlohmann::json PersistenceGate::GetWorkshop() const
	{
		if (!SavingEnabled()) return nlohmann::json::object();
		return _store.GetWorkshop();
	}

	void PersistenceGate::SaveWorkshop(const nlohmann::json& a_workshop, bool a_awaitCommit)
	{
		if (!SavingEnabled()) return;
		_store.SaveWorkshop(a_workshop, a_awaitCommit);
	}

	std::optional<std::string> PersistenceGate::ProfileForConnection(std::uint32_t a_connectionId) const
	{
		const std::scoped_lock lock(_gate);
		const auto found = _profilesByConnection.find(a_connectionId);
		return found == _profilesByConnection.end() ? std::nullopt : std::optional{ found->second };
	}
	void PersistenceGate::SetSavingEnabled(bool a_enabled)
	{
		if (!a_enabled) {
			(void)FlushPendingPlayerStates(true);
		}
		const std::scoped_lock lock(_gate);
		_savingEnabled = a_enabled;
		if (!a_enabled) {
			_pendingPlayerStates.clear();
		}
	}

	bool PersistenceGate::SavingEnabled() const
	{
		const std::scoped_lock lock(_gate);
		return _savingEnabled;
	}
}