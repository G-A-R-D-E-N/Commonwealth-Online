#include "F4TGnsProtocolAdapterImpl.h"

#include "F4TSteamId.h"

namespace F4T::GnsProtocolAdapter
{
	namespace
	{
		std::string FinishHello(Json& a_hello)
		{
			auto text = a_hello.dump();
			if (a_hello.contains("serverPassword")) {
				F4T::Secret::Wipe(a_hello["serverPassword"].get_ref<std::string&>());
			}
			return text;
		}
	}

	InboundResult Session::Impl::HandleWelcome(const Json& packet)
	{
		InboundResult result;
		if (ready || helloSent) {
			result.protocolViolation = true;
			result.error = "duplicate or late welcome packet";
			return result;
		}
		F4T::DifficultyReport::SetServerAcceptsReports(false);
		g_visibilityPauseSupported.store(false, std::memory_order_release);
		const auto protocolVersion = ReadUnsigned32(packet, "protocolVersion");
		const auto playerId = ReadUnsigned32(packet, "playerId");
		if (!protocolVersion || *protocolVersion < 2 || !playerId || *playerId == 0) {
			result.protocolViolation = true;
			result.error = "server does not provide the required Protocol V2 welcome";
			return result;
		}
		if (packet.contains("survivalMode") && !ReadBoolean(packet, "survivalMode")) {
			result.protocolViolation = true;
			result.error = "invalid survivalMode in welcome";
			return result;
		}
		if (packet.contains("requiredDifficulty")) {
			const auto required = ReadUnsigned32(packet, "requiredDifficulty");
			if (!required || *required > 6) {
				result.protocolViolation = true;
				result.error = "invalid requiredDifficulty in welcome";
				return result;
			}
		}
		survivalMode.Update(ReadBoolean(packet, "survivalMode"));

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
			if (clientIdentity->privacy) hello["privacy"] = *clientIdentity->privacy;
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
			if (clientIdentity->hostProof) hello["hostProof"] = *clientIdentity->hostProof;
			if (clientIdentity->nickname && !clientIdentity->nickname->empty()) hello["nickname"] = *clientIdentity->nickname;
			if (clientIdentity->serverPassword) {
				F4T::Secret::Wipe(*clientIdentity->serverPassword);
				clientIdentity->serverPassword.reset();
			}
		}
		if (const auto difficulty = F4T::DifficultyReport::LocalDifficulty()) hello["difficulty"] = *difficulty;
		Detail::AddSlotProtocolToHello(hello);
		if (clientIdentity && clientIdentity->beginSteamTicket && hello.contains("steamId") && !hello.contains("steamTicket")) {
			const auto nonce = ReadString(packet, "steamTicketNonce");
			const auto identity = nonce && F4T::SteamId::IsTicketNonce(*nonce) ?
				F4T::SteamId::BoundWebApiIdentity(*nonce) :
				std::string{ F4T::SteamId::kWebApiIdentity };
			if (auto poll = clientIdentity->beginSteamTicket(identity)) {
				pendingTicket = std::move(poll);
				deferredHello = std::move(hello);
				return result;
			}
		}
		result.controlMessages.push_back({ FinishHello(hello), Delivery::ReliableOrdered });
		return result;
	}

	std::vector<OutboundMessage> Session::Impl::PollDeferredHello()
	{
		if (!pendingTicket || !deferredHello) return {};
		const auto status = pendingTicket();
		if (!status.finished) return {};
		pendingTicket = {};
		auto hello = std::move(*deferredHello);
		deferredHello.reset();
		if (status.hex) hello["steamTicket"] = *status.hex;
		return { OutboundMessage{ FinishHello(hello), Delivery::ReliableOrdered } };
	}

	InboundResult Session::Impl::HandleSessionReady(const Json& packet)
	{
		InboundResult result;
		const auto protocolVersion = ReadUnsigned32(packet, "protocolVersion");
		const auto serverProtocolVersion = ReadUnsigned32(packet, "serverProtocolVersion");
		const auto playerId = ReadUnsigned32(packet, "playerId");
		const bool resumed = ReadBoolean(packet, "resumed").value_or(false);
		if (!helloSent || ready || !protocolVersion || *protocolVersion != 2 ||
			!serverProtocolVersion || *serverProtocolVersion < 2 ||
			!playerId || *playerId == 0 || (!resumed && *playerId != localPlayerId) || !pendingWelcome.is_object()) {
			result.protocolViolation = true;
			result.error = "invalid Protocol V2 sessionReady packet";
			return result;
		}

		if (packet.contains("orderedPlayerVisibility") && !ReadBoolean(packet, "orderedPlayerVisibility")) {
			result.protocolViolation = true;
			result.error = "sessionReady visibility flag is not a boolean";
			return result;
		}
		if (packet.contains("survivalMode") && !ReadBoolean(packet, "survivalMode")) {
			result.protocolViolation = true;
			result.error = "sessionReady survivalMode is not a boolean";
			return result;
		}
		if (packet.contains("requiredDifficulty")) {
			const auto required = ReadUnsigned32(packet, "requiredDifficulty");
			if (!required || *required > 6) {
				result.protocolViolation = true;
				result.error = "sessionReady requiredDifficulty is not 0-6";
				return result;
			}
		}

		const bool orderedVisibility = ReadBoolean(packet, "orderedPlayerVisibility").value_or(false);
		const bool pauseSupported = ReadBoolean(packet, "visibilityPauseSupported").value_or(false);
		const bool downedSupported = ReadBoolean(packet, "authoritativeDownedSupported").value_or(false);
		std::optional<ReadyIdentity> readyIdentity;
		std::string ackPayload;
		std::string readyWelcomeText;
		try {
			if (const auto found = packet.find("resumeAckRequired"); found != packet.end() && !ReadBoolean(packet, "resumeAckRequired")) {
				result.protocolViolation = true;
				result.error = "sessionReady acknowledgement flag is not a boolean";
				return result;
			}
			const bool resumeAckRequired = ReadBoolean(packet, "resumeAckRequired").value_or(false);

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
				const auto multiplayerSaveAdmin = packet.find("multiplayerSaveAdmin") == packet.end() ?
					std::optional<bool>{ false } :
					ReadBoolean(packet, "multiplayerSaveAdmin");
				if (!multiplayerSaveAdmin) {
					result.protocolViolation = true;
					result.error = "sessionReady administrator flag is not a boolean";
					return result;
				}
				if (!readyIdentityCommitter) {
					result.localHandshakeFailure = true;
					result.error = "client identity persistence callback is unavailable";
					return result;
				}

				ReadyIdentity identity{
					.profileId = *profileId,
					.resumeToken = *resumeToken,
					.resumeExpiresAt = *resumeExpiresAt,
					.multiplayerSaveAdmin = *multiplayerSaveAdmin
				};
				if (const auto secret = packet.find("profileSecret"); secret != packet.end() && secret->is_string() &&
					!secret->get_ref<const std::string&>().empty() && secret->get_ref<const std::string&>().size() <= 128) {
					identity.profileSecret = secret->get<std::string>();
				}
				if (resumeAckRequired) {
					Json ack{
						{ "type", "keepAlive" },
						{ "sessionReadyAck", true },
						{ "resumeTokenAck", identity.resumeToken }
					};
					ackPayload = ack.dump();
				}
				readyIdentity = std::move(identity);
			}

			Json readyWelcome = pendingWelcome;
			for (const char* key : { "survivalMode", "survivalNeeds" }) if (const auto setting = ReadBoolean(packet, key)) readyWelcome[key] = *setting; else readyWelcome.erase(key);
			if (const auto required = ReadUnsigned32(packet, "requiredDifficulty")) readyWelcome["requiredDifficulty"] = *required; else readyWelcome.erase("requiredDifficulty");
			readyWelcome["playerId"] = *playerId;
			readyWelcome["resumed"] = resumed;
			if (const auto serverTime = ReadPositiveFiniteDouble(packet, "serverTime")) readyWelcome["serverTime"] = *serverTime;
			if (const auto host = packet.find("worldStateHostPlayerId"); host != packet.end()) {
				readyWelcome["worldStateHostPlayerId"] = *host;
			}
			if (const auto consoleAllowed = packet.find("consoleAllowed"); consoleAllowed != packet.end() && consoleAllowed->is_boolean()) {
				readyWelcome["consoleAllowed"] = *consoleAllowed;
			}
			if (const auto inventory = packet.find("serverInventory"); inventory != packet.end()) {
				if (F4T::InventoryContract::ParseServerInventory(*inventory) || F4T::InventoryContract::ParseInventoryTransferHeader(*inventory)) {
					readyWelcome["serverInventory"] = *inventory;
				} else {
					readyWelcome.erase("serverInventory");
				}
			}
			if (const auto recovery = packet.find("startingInventoryRecovery"); recovery != packet.end()) readyWelcome["startingInventoryRecovery"] = *recovery;
			if (!Detail::ForwardCharacterSlotsToWelcome(packet, readyWelcome, clientIdentity ? clientIdentity->profileId : "")) {
				result.protocolViolation = true; result.error = "sessionReady selected profile does not match the character slot"; return result;
			}
			readyWelcome["protocolVersion"] = 2;
			readyWelcome["serverProtocolVersion"] = *serverProtocolVersion;
			readyWelcomeText = readyWelcome.dump();
		} catch (...) {
			result.protocolViolation = true;
			result.error = "sessionReady contains packet data that cannot be serialised";
			return result;
		}

		if (readyIdentity) {
			std::string commitError;
			if (!readyIdentityCommitter(*readyIdentity, commitError)) {
				result.localHandshakeFailure = true;
				result.error = commitError.empty() ? "failed to persist rotated resume credential" : commitError;
				return result;
			}
			result.readyIdentity = std::move(readyIdentity);
			if (!ackPayload.empty()) {
				result.controlMessages.push_back({ std::move(ackPayload), Delivery::ReliableOrdered });
			}
			result.resumeAcknowledged = true;
		}
		survivalMode.Update(ReadBoolean(packet, "survivalMode"));
		F4T::DifficultyReport::SetServerAcceptsReports(ReadBoolean(packet, "difficultyReportSupported").value_or(false));
		ready = true;
		orderedPlayerVisibility = orderedVisibility;
		visibilityPauseSupported = pauseSupported;
		g_visibilityPauseSupported.store(pauseSupported, std::memory_order_release);
		authoritativeDownedSupported = downedSupported;
		localPlayerId = *playerId;
		pendingWelcome = Json{};
		result.legacyMessages.push_back(std::move(readyWelcomeText));
		return result;
	}

	InboundResult Session::Impl::HandleSessionEnded(const Json& packet)
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

	void Session::Impl::HandleNpcAuthority(const Json& packet)
	{
		auto scope = ReadAuthorityScope(packet);
		const auto epoch = ReadUnsigned64(packet, "authorityEpoch");
		if (!scope || !epoch || *epoch == 0) {
			return;
		}

		const auto holder = ReadUnsigned32(packet, "authorityPlayerId").value_or(0);
		std::erase_if(npcAuthority, [&](const auto& a_entry) {
			return a_entry.second.serverScope == *scope;
		});
		if (holder == 0) {
			return;
		}
		const auto storedScope = [this](ScopeKey a_scope, std::uint32_t a_holder) {
			if (formIdTranslator && a_holder != 0 && a_holder != localPlayerId) {
				a_scope.cellId = TranslateFormIdText(formIdTranslator, a_holder, a_scope.cellId);
				if (!a_scope.worldspaceId.empty()) {
					a_scope.worldspaceId = TranslateFormIdText(formIdTranslator, a_holder, a_scope.worldspaceId);
				}
			}
			return a_scope;
		};
		const auto key = storedScope(*scope, holder);
		const auto existing = npcAuthority.find(key);
		if (existing == npcAuthority.end()) {
			if (npcAuthority.size() >= kMaximumNpcAuthorityScopes) {
				return;
			}
		}
		npcAuthority[key] = NpcAuthorityEntry{ holder, *epoch, *scope };
	}

	InboundResult Session::Impl::HandlePlayerState(const Json& packet)
	{
		InboundResult result;
		const auto playerId = ReadUnsigned32(packet, "playerId");
		if (!playerId || *playerId == 0 || *playerId == localPlayerId) {
			return result;
		}
		if (!ReserveRemotePlayer(*playerId)) {
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
		if (packet.contains("displayName") && !packet.contains("factionTag")) {
			durable.erase("factionTag");
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
		synthesized.erase("serverDowned");
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

	InboundResult Session::Impl::HandleTransform(const Json& packet)
	{
		InboundResult result;
		const auto playerId = ReadUnsigned32(packet, "playerId");
		if (!playerId || *playerId == 0 || !ReserveRemotePlayer(*playerId)) {
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

	InboundResult Session::Impl::AdaptInbound(std::string_view a_jsonPayload)
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
		if (type && *type == "visibilityResumed") {
			if (ReadUnsigned64(packet, "movementEpoch") == movementEpoch && resumeTransitionId != 0 &&
				ReadUnsigned64(packet, "resumeTransitionId") == resumeTransitionId) {
				awaitingVisibilityResume = false;
				resumeStatePending = false;
				resumeMovementType.reset();
			}
			return result;
		}
		if (type && *type == "capabilities" && packet.contains("survivalMode")) {
			const auto setting = ReadBoolean(packet, "survivalMode");
			if (!setting) {
				result.protocolViolation = true;
				result.error = "invalid survivalMode in server settings";
				return result;
			}
			survivalMode.Update(*setting);
		}
		if (type && *type == "capabilities" && packet.contains("requiredDifficulty")) {
			const auto required = ReadUnsigned32(packet, "requiredDifficulty");
			if (!required || *required > 6) {
				result.protocolViolation = true;
				result.error = "invalid requiredDifficulty in server settings";
				return result;
			}
		}
		if (orderedPlayerVisibility && type && (*type == "transform" || *type == "playerState" || *type == "pluginList" || *type == "disconnect" || *type == "peerHidden")) {
			const auto playerId = ReadUnsigned32(packet, "playerId");
			if (playerId && *playerId != 0 && *playerId != localPlayerId) {
				const auto incarnation = ReadUnsigned64(packet, "playerIncarnation");
				if (*playerId > (std::numeric_limits<std::uint16_t>::max)() || !incarnation || *incarnation == 0 ||
					*incarnation < playerIncarnations[*playerId]) {
					return result;
				}
				if (*incarnation > playerIncarnations[*playerId]) {
					if (playerIncarnations[*playerId] != 0) {
						ForgetRemotePlayer(*playerId);
						if (*type != "disconnect") {
							result.legacyMessages.push_back(Json{ { "type", "disconnect" }, { "playerId", *playerId } }.dump());
						}
					}
					playerIncarnations[*playerId] = *incarnation;
				}
			}
		}
		if (type && *type == "npcAuthority") {
			HandleNpcAuthority(packet);
			return result;
		}
		if (type && (*type == "disconnect" || *type == "peerHidden")) {
			const auto playerId = ReadUnsigned32(packet, "playerId");
			const auto sequence = ReadUnsigned64(packet, "visibilitySequence");
			if (!playerId || *playerId == 0 || (orderedPlayerVisibility && (!sequence || *sequence == 0))) {
				return result;
			}
			if (orderedPlayerVisibility) {
				if (*playerId > (std::numeric_limits<std::uint16_t>::max)() || *sequence <= visibilitySequences[*playerId]) {
					return result;
				}
				visibilitySequences[*playerId] = *sequence;
			}
			if (*type == "disconnect") {
				HandleRemotePlayerDisconnect(packet);
			} else {
				latestRemoteTransform.erase(*playerId);
				pendingRemoteActionEvents.erase(*playerId);
				pendingRemoteVitals.erase(*playerId);
			}
			result.legacyMessages.push_back(packet.dump());
			return result;
		}
		if (type && *type == "playerRoster") {
			HandlePlayerRoster(packet);
			result.legacyMessages.push_back(packet.dump());
			return result;
		}
		if (type && *type == "playerState") {
			auto state = HandlePlayerState(packet);
			result.legacyMessages.insert(result.legacyMessages.end(), state.legacyMessages.begin(), state.legacyMessages.end());
			return result;
		}
		if (type && *type == "transform") {
			if (orderedPlayerVisibility) {
				const auto playerId = ReadUnsigned32(packet, "playerId");
				const auto sequence = ReadUnsigned64(packet, "visibilitySequence");
				if (!playerId || *playerId == 0 || *playerId > (std::numeric_limits<std::uint16_t>::max)() ||
					!sequence || *sequence <= visibilitySequences[*playerId]) {
					return result;
				}
				visibilitySequences[*playerId] = *sequence;
			}
			Json safePacket = packet;
			if (!authoritativeDownedSupported) safePacket.erase("serverDowned");
			auto transform = HandleTransform(safePacket);
			result.legacyMessages.insert(result.legacyMessages.end(), transform.legacyMessages.begin(), transform.legacyMessages.end());
			return result;
		}
		if (!authoritativeDownedSupported && type && (*type == "downedState" || *type == "playerDied")) {
			Json safePacket = packet;
			safePacket.erase("serverDowned");
			safePacket.erase("playerIncarnation");
			result.legacyMessages.push_back(safePacket.dump());
			return result;
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

}
