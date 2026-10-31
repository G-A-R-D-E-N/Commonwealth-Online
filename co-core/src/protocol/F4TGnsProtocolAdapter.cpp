#include "F4TGnsProtocolAdapter.h"

#include "F4TInventoryContract.h"
#include "F4TGnsProtocolAdapterModpack.h"
#include "F4TGnsProtocolAdapterSlots.h"
#include "F4TSecretWipe.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace
{
	using Json = nlohmann::json;
	using Delivery = F4T::GnsProtocolAdapter::Delivery;
	using OutboundMessage = F4T::GnsProtocolAdapter::OutboundMessage;
	using NpcAuthorityAssignment = F4T::GnsProtocolAdapter::NpcAuthorityAssignment;
	constexpr std::size_t kMaximumPendingRemoteActionEvents = 64;
	constexpr std::array<const char*, 4> kStickyPlayerStateFields{ "equippedItems", "appearance", "characterName", "powerArmor" };

	struct ScopeKey
	{
		std::string cellId;
		std::string worldspaceId;
		bool operator==(const ScopeKey&) const = default;
	};

	struct ScopeKeyHash
	{
		std::size_t operator()(const ScopeKey& a_scope) const noexcept
		{
			const auto first = std::hash<std::string>{}(a_scope.cellId);
			const auto second = std::hash<std::string>{}(a_scope.worldspaceId);
			return first ^ (second + 0x9e3779b9U + (first << 6U) + (first >> 2U));
		}
	};

	std::string TrimLegacyPayload(std::string_view a_payload)
	{
		while (!a_payload.empty() &&
		       (a_payload.back() == '\n' || a_payload.back() == '\r' || a_payload.back() == ' ' || a_payload.back() == '\t')) {
			a_payload.remove_suffix(1);
		}
		while (!a_payload.empty() &&
		       (a_payload.front() == '\n' || a_payload.front() == '\r' || a_payload.front() == ' ' || a_payload.front() == '\t')) {
			a_payload.remove_prefix(1);
		}
		return std::string{ a_payload };
	}

	std::optional<std::string> ReadString(const Json& a_packet, const char* a_field)
	{
		const auto iterator = a_packet.find(a_field);
		if (iterator == a_packet.end() || !iterator->is_string()) {
			return std::nullopt;
		}
		return iterator->get<std::string>();
	}

	std::optional<bool> ReadBoolean(const Json& a_packet, const char* a_field)
	{
		const auto iterator = a_packet.find(a_field);
		if (iterator == a_packet.end() || !iterator->is_boolean()) {
			return std::nullopt;
		}
		return iterator->get<bool>();
	}

	std::optional<std::uint64_t> ReadUnsigned64(const Json& a_packet, const char* a_field)
	{
		const auto iterator = a_packet.find(a_field);
		if (iterator == a_packet.end()) {
			return std::nullopt;
		}

		try {
			if (iterator->is_number_unsigned()) {
				return iterator->get<std::uint64_t>();
			}
			if (iterator->is_number_integer()) {
				const auto value = iterator->get<std::int64_t>();
				if (value >= 0) {
					return static_cast<std::uint64_t>(value);
				}
			}
			if (iterator->is_string()) {
				const auto text = iterator->get<std::string>();
				std::uint64_t value = 0;
				const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
				if (error == std::errc{} && end == text.data() + text.size()) {
					return value;
				}
			}
		} catch (...) {
		}
		return std::nullopt;
	}

	std::optional<std::uint32_t> ReadUnsigned32(const Json& a_packet, const char* a_field)
	{
		const auto value = ReadUnsigned64(a_packet, a_field);
		if (!value || *value > (std::numeric_limits<std::uint32_t>::max)()) {
			return std::nullopt;
		}
		return static_cast<std::uint32_t>(*value);
	}

	std::optional<double> ReadPositiveFiniteDouble(const Json& a_packet, const char* a_field)
	{
		const auto iterator = a_packet.find(a_field);
		if (iterator == a_packet.end() || iterator->is_boolean() || !iterator->is_number()) {
			return std::nullopt;
		}
		try {
			const auto value = iterator->get<double>();
			if (std::isfinite(value) && value > 0.0) {
				return value;
			}
		} catch (...) {
		}
		return std::nullopt;
	}


	std::optional<ScopeKey> ReadAuthorityScope(const Json& a_packet)
	{
		const auto cellId = ReadString(a_packet, "authorityCellId");
		if (!cellId || cellId->empty()) {
			return std::nullopt;
		}
		return ScopeKey{ *cellId, ReadString(a_packet, "authorityWorldspaceId").value_or("") };
	}

	std::string TranslateFormIdText(
		const F4T::GnsProtocolAdapter::FormIdTranslator& a_translator,
		std::uint32_t a_playerId,
		const std::string& a_text)
	{
		if (a_text.empty() || a_text.size() > 8) {
			return a_text;
		}
		std::uint32_t value = 0;
		const auto [end, error] = std::from_chars(a_text.data(), a_text.data() + a_text.size(), value, 16);
		if (error != std::errc{} || end != a_text.data() + a_text.size()) {
			return a_text;
		}
		const auto translated = a_translator(a_playerId, value);
		if (translated == value) {
			return a_text;
		}
		char buffer[9]{};
		std::snprintf(buffer, sizeof(buffer), "%08X", translated);
		return buffer;
	}

	std::optional<ScopeKey> ReadNpcScope(const Json& a_npc)
	{
		const auto cellId = ReadString(a_npc, "cellId");
		if (!cellId || cellId->empty()) {
			return std::nullopt;
		}
		return ScopeKey{ *cellId, ReadString(a_npc, "worldspaceId").value_or("") };
	}

	void MergeStickyPlayerState(Json& a_transform, const Json& a_sticky)
	{
		for (const char* field : kStickyPlayerStateFields) {
			if (const auto iterator = a_sticky.find(field); iterator != a_sticky.end()) {
				a_transform[field] = *iterator;
			}
		}
	}

	void AppendPendingActionEvents(Json& a_pending, const Json& a_actions)
	{
		if (!a_pending.is_array()) {
			a_pending = Json::array();
		}
		for (const auto& action : a_actions) {
			if (a_pending.size() >= kMaximumPendingRemoteActionEvents) {
				a_pending.erase(a_pending.begin());
			}
			a_pending.push_back(action);
		}
	}
}

namespace F4T::GnsProtocolAdapter
{
	class Session::Impl
	{
	public:
		bool ready{ false };
		bool helloSent{ false };
		std::uint32_t localPlayerId{};
		std::optional<ClientIdentity> clientIdentity;
		ReadyIdentityCommitter readyIdentityCommitter;
		FormIdTranslator formIdTranslator;
		Json pendingWelcome;
		std::optional<Json> lastLocalAppearance;
		std::optional<std::string> lastLocalCharacterName;
		std::string lastLocalDurableDump;
		std::unordered_map<std::uint32_t, Json> remoteDurablePlayerState;
		std::unordered_map<std::uint32_t, Json> latestRemoteTransform;
		std::unordered_map<std::uint32_t, Json> pendingRemoteActionEvents;
		std::unordered_map<std::uint32_t, Json> pendingRemoteVitals;
		std::unordered_map<ScopeKey, NpcAuthorityAssignment, ScopeKeyHash> npcAuthority;
		std::optional<F4T::WorldPolicy::Policy> worldPolicy;
		F4T::WorldPolicy::Reassembler worldPolicyReassembler;

		void Reset()
		{
			ready = false;
			helloSent = false;
			localPlayerId = 0;
			if (clientIdentity && clientIdentity->serverPassword) {
				F4T::Secret::Wipe(*clientIdentity->serverPassword);
			}
			clientIdentity.reset();
			readyIdentityCommitter = {};
			formIdTranslator = {};
			pendingWelcome = Json{};
			lastLocalAppearance.reset();
			lastLocalCharacterName.reset();
			lastLocalDurableDump.clear();
			remoteDurablePlayerState.clear();
			latestRemoteTransform.clear();
			pendingRemoteActionEvents.clear();
			pendingRemoteVitals.clear();
			npcAuthority.clear();
			worldPolicy.reset();
			worldPolicyReassembler.Reset();
		}

		std::vector<OutboundMessage> AdaptTransform(Json packet)
		{
			Json equippedItems = Json::array();
			if (const auto iterator = packet.find("equippedItems"); iterator != packet.end() && iterator->is_array()) {
				equippedItems = *iterator;
			}

			if (const auto iterator = packet.find("appearance"); iterator != packet.end() && iterator->is_object()) {
				lastLocalAppearance = *iterator;
			}
			if (const auto characterName = ReadString(packet, "characterName"); characterName && !characterName->empty()) {
				lastLocalCharacterName = *characterName;
			}

			Json actionEvents = Json::array();
			if (const auto iterator = packet.find("actionEvents"); iterator != packet.end() && iterator->is_array()) {
				actionEvents = *iterator;
			}

			Json durable = Json::object();
			durable["equippedItems"] = equippedItems;
			if (const auto iterator = packet.find("powerArmor"); iterator != packet.end() && iterator->is_object()) {
				durable["powerArmor"] = *iterator;
			}
			if (lastLocalAppearance) {
				durable["appearance"] = *lastLocalAppearance;
			}
			if (lastLocalCharacterName && !lastLocalCharacterName->empty()) {
				durable["characterName"] = *lastLocalCharacterName;
			}
			const auto durableDump = durable.dump();

			for (const char* field : { "equippedItems", "appearance", "actionEvents", "characterName", "powerArmor" }) {
				packet.erase(field);
			}

			std::vector<OutboundMessage> messages;
			messages.push_back({ packet.dump(), Delivery::UnreliableSequenced });
			if (durableDump != lastLocalDurableDump || !actionEvents.empty()) {
				Json playerState{
					{ "type", "playerState" },
					{ "actionEvents", std::move(actionEvents) }
				};
				MergeStickyPlayerState(playerState, durable);
				messages.push_back({ playerState.dump(), Delivery::ReliableOrdered });
				lastLocalDurableDump = durableDump;
			}
			return messages;
		}

		std::vector<OutboundMessage> AdaptNpcState(const Json& packet)
		{
			const auto npcs = packet.find("npcs");
			if (npcs == packet.end() || !npcs->is_array() || npcs->empty() || localPlayerId == 0) {
				return {};
			}

			std::unordered_map<ScopeKey, Json, ScopeKeyHash> groups;
			for (const auto& npc : *npcs) {
				if (!npc.is_object()) {
					continue;
				}
				const auto scope = ReadNpcScope(npc);
				if (!scope) {
					continue;
				}
				auto& group = groups[*scope];
				if (!group.is_array()) {
					group = Json::array();
				}
				group.push_back(npc);
			}

			std::vector<OutboundMessage> messages;
			for (auto& [scope, group] : groups) {
				const auto authority = npcAuthority.find(scope);
				if (authority == npcAuthority.end() ||
					authority->second.playerId != localPlayerId ||
					authority->second.epoch == 0) {
					continue;
				}

				Json scopedPacket = packet;
				scopedPacket["npcs"] = std::move(group);
				scopedPacket["authorityEpoch"] = authority->second.epoch;
				scopedPacket["authorityCellId"] = scope.cellId;
				scopedPacket["authorityWorldspaceId"] = scope.worldspaceId;
				messages.push_back({ scopedPacket.dump(), Delivery::UnreliableSequenced });
			}
			return messages;
		}

		std::vector<OutboundMessage> AdaptOutbound(std::string_view a_legacyPayload)
		{
			const auto text = TrimLegacyPayload(a_legacyPayload);
			if (text.empty() || !ready) {
				return {};
			}

			Json packet;
			try {
				packet = Json::parse(text);
			} catch (...) {
				return { { text, Delivery::ReliableOrdered } };
			}
			if (!packet.is_object()) {
				return { { text, Delivery::ReliableOrdered } };
			}

			const auto type = ReadString(packet, "type");
			if (type && *type == "transform") {
				return AdaptTransform(std::move(packet));
			}
			if (type && *type == "npcState") {
				return AdaptNpcState(packet);
			}
			return { { packet.dump(), Delivery::ReliableOrdered } };
		}

		InboundResult HandleWelcome(const Json& packet)
		{
			InboundResult result;
			if (ready || helloSent) {
				result.protocolViolation = true;
				result.error = "duplicate or late welcome packet";
				return result;
			}

			const auto protocolVersion = ReadUnsigned32(packet, "protocolVersion");
			const auto playerId = ReadUnsigned32(packet, "playerId");
			if (!protocolVersion || *protocolVersion < 2 || !playerId || *playerId == 0) {
				result.protocolViolation = true;
				result.error = "server does not provide the required Protocol V2 welcome";
				return result;
			}

			localPlayerId = *playerId;
			pendingWelcome = packet;
			helloSent = true;

			Json hello{
				{ "type", "hello" },
				{ "protocolVersion", 2 }
			};
			if (clientIdentity) {
				const auto fields = ClientCompatibility::HelloFields(clientIdentity->metadata);
				if (!fields || clientIdentity->profileId.empty()) {
					result.protocolViolation = true;
					result.error = "client identity metadata is incomplete; cannot satisfy Protocol V2 identity";
					return result;
				}
				for (const auto& [key, value] : fields->items()) {
					hello[key] = value;
				}
				hello["profileId"] = clientIdentity->profileId;
				if (clientIdentity->resumeToken) {
					hello["resumeToken"] = *clientIdentity->resumeToken;
				}
				if (clientIdentity->serverPassword) {
					hello["serverPassword"] = *clientIdentity->serverPassword;
				}
				if (clientIdentity->modpackFingerprint) {
					hello["modpackFingerprint"] = *clientIdentity->modpackFingerprint;
				}
				if (clientIdentity->saveAttestation) {
					hello["saveAttestation"] = Json{
						{ "saveFile", clientIdentity->saveAttestation->saveFile },
						{ "sha256", clientIdentity->saveAttestation->sha256Hex }
					};
				}
				if (clientIdentity->profileSecret) hello["profileSecret"] = *clientIdentity->profileSecret;
				if (clientIdentity->steamId) hello["steamId"] = *clientIdentity->steamId;
				if (clientIdentity->steamTicket) hello["steamTicket"] = *clientIdentity->steamTicket;
				if (clientIdentity->serverPassword) {
					F4T::Secret::Wipe(*clientIdentity->serverPassword);
					clientIdentity->serverPassword.reset();
				}
			}
			Detail::AddSlotProtocolToHello(hello);
			auto helloText = hello.dump();
			if (hello.contains("serverPassword")) {
				F4T::Secret::Wipe(hello["serverPassword"].get_ref<std::string&>());
			}
			result.controlMessages.push_back({ std::move(helloText), Delivery::ReliableOrdered });
			return result;
		}

		InboundResult HandleSessionReady(const Json& packet)
		{
			InboundResult result;
			const auto protocolVersion = ReadUnsigned32(packet, "protocolVersion");
			const auto serverProtocolVersion = ReadUnsigned32(packet, "serverProtocolVersion");
			const auto playerId = ReadUnsigned32(packet, "playerId");
			if (!helloSent || ready || !protocolVersion || *protocolVersion != 2 ||
				!serverProtocolVersion || *serverProtocolVersion < 2 ||
				!playerId || *playerId == 0 || *playerId != localPlayerId || !pendingWelcome.is_object()) {
				result.protocolViolation = true;
				result.error = "invalid Protocol V2 sessionReady packet";
				return result;
			}

			if (clientIdentity) {
				const auto profileId = ReadString(packet, "profileId");
				const auto clientVersion = ReadString(packet, "clientVersion");
				const auto runtimeVersion = ReadString(packet, "falloutRuntimeVersion");
				const auto fingerprint = ReadString(packet, "contentManifestFingerprint");
				const auto resumeToken = ReadString(packet, "resumeToken");
				const auto resumeExpiresAt = ReadPositiveFiniteDouble(packet, "resumeExpiresAt");
				if (!profileId || *profileId != clientIdentity->profileId ||
					!clientVersion || *clientVersion != clientIdentity->metadata.clientVersion ||
					!runtimeVersion || *runtimeVersion != clientIdentity->metadata.falloutRuntimeVersion ||
					!fingerprint || *fingerprint != clientIdentity->metadata.contentManifestFingerprint ||
					!resumeToken || resumeToken->empty() || !resumeExpiresAt) {
					result.protocolViolation = true;
					result.error = "sessionReady is missing or mismatches the configured client identity state";
					return result;
				}
				if (!readyIdentityCommitter) {
					result.localHandshakeFailure = true;
					result.error = "client identity persistence callback is unavailable";
					return result;
				}

				ReadyIdentity readyIdentity{
					.profileId = *profileId,
					.resumeToken = *resumeToken,
					.resumeExpiresAt = *resumeExpiresAt,
					.multiplayerSaveAdmin = ReadBoolean(packet, "multiplayerSaveAdmin").value_or(false)
				};
				if (const auto secret = packet.find("profileSecret"); secret != packet.end() && secret->is_string() &&
					!secret->get_ref<const std::string&>().empty() && secret->get_ref<const std::string&>().size() <= 128) {
					readyIdentity.profileSecret = secret->get<std::string>();
				}
				std::string commitError;
				if (!readyIdentityCommitter(readyIdentity, commitError)) {
					result.localHandshakeFailure = true;
					result.error = commitError.empty() ? "failed to persist rotated resume credential" : commitError;
					return result;
				}
				if (packet.value("resumeAckRequired", false)) {
					Json ack{
						{ "type", "keepAlive" },
						{ "sessionReadyAck", true },
						{ "resumeTokenAck", *resumeToken }
					};
					result.controlMessages.push_back({ ack.dump(), Delivery::ReliableOrdered });
				}
				result.readyIdentity = std::move(readyIdentity);
			}
			if (const auto host = packet.find("worldStateHostPlayerId"); host != packet.end()) {
				pendingWelcome["worldStateHostPlayerId"] = *host;
			}
			if (const auto consoleAllowed = packet.find("consoleAllowed"); consoleAllowed != packet.end() && consoleAllowed->is_boolean()) {
				pendingWelcome["consoleAllowed"] = *consoleAllowed;
			}
			if (const auto inventory = packet.find("serverInventory"); inventory != packet.end()) {
				if (F4T::InventoryContract::ParseServerInventory(*inventory) || F4T::InventoryContract::ParseInventoryTransferHeader(*inventory)) {
					pendingWelcome["serverInventory"] = *inventory;
				} else {
					pendingWelcome.erase("serverInventory");
				}
			}
			Detail::ForwardCharacterSlotsToWelcome(packet, pendingWelcome);
			pendingWelcome["protocolVersion"] = 2;
			pendingWelcome["serverProtocolVersion"] = *serverProtocolVersion;
			ready = true;
			result.legacyMessages.push_back(pendingWelcome.dump());
			pendingWelcome = Json{};
			return result;
		}

		InboundResult HandleSessionEnded(const Json& packet)
		{
			InboundResult result;
			if (ready) {
				result.legacyMessages.push_back(packet.dump());
				return result;
			}
			SessionRejection rejection{
				ReadString(packet, "code").value_or(""),
				ReadString(packet, "reason").value_or("")
			};
			rejection.requiredModpack = Detail::ReadRequiredModpack(packet);
			rejection.requiredModpackFingerprint = Detail::ReadRequiredModpackFingerprint(packet);
			rejection.requiredModpackName = Detail::ReadRequiredModpackName(packet);
			if (!rejection.requiredModpack) {
				rejection.requiredModpackChunks = Detail::ReadRequiredModpackChunks(packet);
			}
			result.rejection = std::move(rejection);
			return result;
		}

		void HandleNpcAuthority(const Json& packet)
		{
			auto scope = ReadAuthorityScope(packet);
			const auto epoch = ReadUnsigned64(packet, "authorityEpoch");
			if (!scope || !epoch || *epoch == 0) {
				return;
			}

			const auto holder = ReadUnsigned32(packet, "authorityPlayerId").value_or(0);
			if (formIdTranslator && holder != 0 && holder != localPlayerId) {
				scope->cellId = TranslateFormIdText(formIdTranslator, holder, scope->cellId);
				if (!scope->worldspaceId.empty()) {
					scope->worldspaceId = TranslateFormIdText(formIdTranslator, holder, scope->worldspaceId);
				}
			}
			npcAuthority[*scope] = NpcAuthorityAssignment{ holder, *epoch };
		}

		InboundResult HandlePlayerState(const Json& packet)
		{
			InboundResult result;
			const auto playerId = ReadUnsigned32(packet, "playerId");
			if (!playerId || *playerId == 0 || *playerId == localPlayerId) {
				return result;
			}

			auto& durable = remoteDurablePlayerState[*playerId];
			if (!durable.is_object()) {
				durable = Json::object();
			}
			for (const char* field : kStickyPlayerStateFields) {
				if (const auto iterator = packet.find(field); iterator != packet.end()) {
					durable[field] = *iterator;
				}
			}

			const auto actions = packet.find("actionEvents");
			const bool hasActions = actions != packet.end() && actions->is_array() && !actions->empty();
			const auto vitals = packet.find("vitals");
			const auto transform = latestRemoteTransform.find(*playerId);
			if (transform == latestRemoteTransform.end()) {
				if (vitals != packet.end()) {
					pendingRemoteVitals[*playerId] = *vitals;
				}
				if (hasActions) {
					AppendPendingActionEvents(pendingRemoteActionEvents[*playerId], *actions);
				}
				return result;
			}

			Json synthesized = transform->second;
			MergeStickyPlayerState(synthesized, durable);
			if (vitals != packet.end()) {
				synthesized["vitals"] = *vitals;
			}
			if (hasActions) {
				synthesized["actionEvents"] = *actions;
			}
			result.legacyMessages.push_back(synthesized.dump());
			return result;
		}

		InboundResult HandleTransform(const Json& packet)
		{
			InboundResult result;
			const auto playerId = ReadUnsigned32(packet, "playerId");
			if (!playerId || *playerId == 0) {
				result.legacyMessages.push_back(packet.dump());
				return result;
			}

			latestRemoteTransform[*playerId] = packet;
			Json merged = packet;
			if (const auto durable = remoteDurablePlayerState.find(*playerId); durable != remoteDurablePlayerState.end()) {
				MergeStickyPlayerState(merged, durable->second);
			}
			if (const auto vitals = pendingRemoteVitals.find(*playerId); vitals != pendingRemoteVitals.end()) {
				merged["vitals"] = vitals->second;
				pendingRemoteVitals.erase(vitals);
			}
			if (const auto pending = pendingRemoteActionEvents.find(*playerId); pending != pendingRemoteActionEvents.end()) {
				if (pending->second.is_array() && !pending->second.empty()) {
					merged["actionEvents"] = pending->second;
				}
				pendingRemoteActionEvents.erase(pending);
			}
			result.legacyMessages.push_back(merged.dump());
			return result;
		}

		InboundResult AdaptInbound(std::string_view a_jsonPayload)
		{
			InboundResult result;
			Json packet;
			try {
				packet = Json::parse(a_jsonPayload);
			} catch (...) {
				if (!ready) {
					result.protocolViolation = true;
					result.error = "invalid JSON during Protocol V2 handshake";
				} else {
					result.legacyMessages.emplace_back(a_jsonPayload);
				}
				return result;
			}

			if (!packet.is_object()) {
				if (!ready) {
					result.protocolViolation = true;
					result.error = "non-object packet during Protocol V2 handshake";
				} else {
					result.legacyMessages.push_back(packet.dump());
				}
				return result;
			}

			const auto type = ReadString(packet, "type");
			if (type && *type == "welcome") {
				return HandleWelcome(packet);
			}
			if (type && *type == "sessionReady") {
				return HandleSessionReady(packet);
			}
			if (type && *type == "sessionEnded") {
				return HandleSessionEnded(packet);
			}
			if (type && *type == "sessionState") {
				result.legacyMessages.push_back(packet.dump());
				return result;
			}
			if (!ready) {
				result.protocolViolation = true;
				result.error = "server sent gameplay traffic before Protocol V2 sessionReady";
				return result;
			}
			if (type && *type == "npcAuthority") {
				HandleNpcAuthority(packet);
				return result;
			}
			if (type && *type == "playerState") {
				return HandlePlayerState(packet);
			}
			if (type && *type == "transform") {
				return HandleTransform(packet);
			}
			if (type && (*type == "worldPolicy" || *type == F4T::WorldPolicy::kPartialMessageType)) {
				if (const auto policy = worldPolicyReassembler.Apply(packet)) {
					worldPolicy = *policy;
				}
				return result;
			}

			result.legacyMessages.push_back(packet.dump());
			return result;
		}
	};

	Session::Session() :
		_impl(std::make_unique<Impl>())
	{}

	Session::~Session() = default;
	Session::Session(Session&&) noexcept = default;
	Session& Session::operator=(Session&&) noexcept = default;

	void Session::Reset()
	{
		_impl->Reset();
	}

	void Session::SetClientIdentity(ClientIdentity a_identity, ReadyIdentityCommitter a_committer)
	{
		_impl->clientIdentity = std::move(a_identity);
		_impl->readyIdentityCommitter = std::move(a_committer);
	}

	void Session::SetFormIdTranslator(FormIdTranslator a_translator)
	{
		_impl->formIdTranslator = std::move(a_translator);
	}

	bool Session::IsReady() const noexcept
	{
		return _impl && _impl->ready;
	}

	std::vector<OutboundMessage> Session::AdaptOutbound(std::string_view a_legacyPayload)
	{
		return _impl->AdaptOutbound(a_legacyPayload);
	}

	InboundResult Session::AdaptInbound(std::string_view a_jsonPayload)
	{
		return _impl->AdaptInbound(a_jsonPayload);
	}

	std::optional<NpcAuthorityAssignment> Session::GetNpcAuthority(
		std::string_view a_cellId,
		std::string_view a_worldspaceId) const
	{
		if (!_impl || a_cellId.empty()) {
			return std::nullopt;
		}
		const auto found = _impl->npcAuthority.find(ScopeKey{ std::string{ a_cellId }, std::string{ a_worldspaceId } });
		return found == _impl->npcAuthority.end() ? std::nullopt : std::optional{ found->second };
	}

	std::optional<F4T::WorldPolicy::Policy> Session::GetWorldPolicy() const
	{
		return _impl ? _impl->worldPolicy : std::nullopt;
	}
}
