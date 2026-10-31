	public:
		bool EnableObjectState()
		{
			if (objectStateEnabled || hasEverActivatedSession) return false;
			objectStateEnabled = true;
			return true;
		}

	private:
		bool objectStateEnabled{};
		F4T::ObjectState::ObjectStateTable objectStates{ F4T::ObjectState::kMaximumObjectsPerPlayer };

		bool ObjectStateNegotiated(const Session& a_session) const
		{
			return objectStateEnabled && a_session.compatibilityCapabilities.Has("object-state-v1");
		}

		bool ObjectStateVisible(
			const std::optional<F4T::InterestScope::StateScope>& a_viewer,
			const F4T::ObjectWire::ObjectPosition& a_position) const
		{
			if (!a_viewer) return true;
			return F4T::InterestScope::StatesShareInterest(
				a_viewer,
				F4T::InterestScope::StateScope{ a_position.cellId, a_position.worldspaceId, a_position.x, a_position.y });
		}

		std::vector<OutboundFrame> ExistingObjectStateFrames(std::uint32_t a_connectionId, const Session& a_target) const
		{
			if (!ObjectStateNegotiated(a_target)) return {};
			const auto viewer = InterestScopeFromMovement(a_target.lastMovement);
			std::vector<OutboundFrame> frames;
			std::vector<F4T::ObjectWire::ObjectState> chunk;
			chunk.reserve(F4T::ObjectWire::kMaximumSnapshotObjects);
			for (auto state : objectStates.Snapshot(objectStates.Size())) {
				if (!ObjectStateVisible(viewer, state.position)) continue;
				chunk.push_back(std::move(state));
				if (chunk.size() < F4T::ObjectWire::kMaximumSnapshotObjects) continue;
				frames.push_back(ReliableFrame(a_connectionId, F4T::ObjectWire::BuildStateSnapshotPacket(chunk)));
				chunk.clear();
			}
			if (!chunk.empty()) {
				frames.push_back(ReliableFrame(a_connectionId, F4T::ObjectWire::BuildStateSnapshotPacket(chunk)));
			}
			return frames;
		}

		std::vector<OutboundFrame> ExistingWelcomeStateFrames(
			std::uint32_t a_connectionId,
			Session& a_target,
			double a_unixTimeSeconds)
		{
			auto frames = ExistingPlayerStateFrames(a_connectionId, a_target, a_unixTimeSeconds);
			AppendFrames(frames, ExistingObjectStateFrames(a_connectionId, a_target));
			AppendFrames(frames, ExistingQuestFrames(a_connectionId, a_target, a_unixTimeSeconds));
			AppendFrames(frames, WorldPolicyFrames(a_connectionId));
			AppendFrames(frames, ExistingWorkshopSnapshotFrames(a_connectionId, a_target));
			return frames;
		}

		std::vector<OutboundFrame> HandleObjectStateRequest(
			std::uint32_t a_connectionId,
			Session& a_session,
			const Json& a_packet,
			double a_monotonicSeconds)
		{
			const bool activate = a_packet.value("type", std::string{}) == "objectActivateRequest";
			const auto request = activate ?
				F4T::ObjectWire::NormalizeActivateRequest(a_packet) :
				F4T::ObjectWire::NormalizeLockRequest(a_packet);
			if (!request) {
				Reject();
				return {};
			}
			stats.objectStateReceived++;
			const auto operationId = ReadString(*request, "operationId").value_or("");
			const auto rejected = [&](const char* a_reason) {
				stats.objectStateRejected++;
				return std::vector<OutboundFrame>{ ReliableFrame(a_connectionId, Json{
					{ "type", "objectStateRejected" },
					{ "operationId", operationId },
					{ "reason", a_reason } }) };
			};
			if (!ObjectStateNegotiated(a_session)) return rejected("objectStateUnavailable");
			if (!F4T::PacketRateLimit::TakeToken(a_session.objectStateRequests, a_monotonicSeconds, kObjectIntentRefillPerSecond, kObjectIntentBurst)) {
				return rejected("rateLimited");
			}
			const auto object = F4T::ObjectWire::ParseObjectKey(request->at("object"));
			const auto position = F4T::ObjectWire::ParsePosition(request->at("position"));
			if (!object || !position) {
				Reject();
				return {};
			}
			if (!a_session.lastMovement || !a_session.lastMovementMonotonic) return rejected("noTransform");
			const auto age = a_monotonicSeconds - *a_session.lastMovementMonotonic;
			if (!(age >= 0.0) || age > kMaximumCombatPositionAgeSeconds) return rejected("staleTransform");
			if (!ObjectStateVisible(InterestScopeFromMovement(a_session.lastMovement), *position)) return rejected("outOfScope");
			const auto& player = *a_session.lastMovement;
			const auto dx = player.x - position->x;
			const auto dy = player.y - position->y;
			const auto dz = player.z - position->z;
			const auto distance = std::sqrt(dx * dx + dy * dy + dz * dz);
			if (!std::isfinite(distance) || distance > kMaximumObjectReach) return rejected("outOfReach");

			F4T::ObjectState::IntentResult result;
			if (activate) {
				const auto kind = F4T::ObjectWire::ParseKind(request->at("kind").get_ref<const std::string&>());
				const auto action = F4T::ObjectWire::ParseAction(request->at("action").get_ref<const std::string&>());
				if (!kind || !action) {
					Reject();
					return {};
				}
				result = objectStates.ApplyActivate(a_session.playerId, operationId, *object, *kind, *action, *position);
			} else {
				std::optional<std::uint32_t> lockLevel;
				if (request->contains("lockLevel")) lockLevel = request->at("lockLevel").get<std::uint32_t>();
				result = objectStates.ApplyLock(a_session.playerId, operationId, *object, request->at("locked").get<bool>(), lockLevel, *position);
			}
			if (const auto* reason = ObjectStateRejectReason(result.outcome)) return rejected(reason);
			if (result.replayed) {
				stats.objectStateReplayed++;
				return { ReliableFrame(a_connectionId, F4T::ObjectWire::BuildObjectState(result.state)) };
			}
			const auto state = F4T::ObjectWire::BuildObjectState(result.state);
			std::vector<OutboundFrame> frames;
			for (const auto& [connectionId, recipient] : sessions) {
				if (!recipient.active || !ObjectStateNegotiated(recipient)) continue;
				if (!ObjectStateVisible(InterestScopeFromMovement(recipient.lastMovement), result.state.position)) continue;
				frames.push_back(ReliableFrame(connectionId, state));
			}
			stats.objectStateRouted++;
			return frames;
		}

		std::optional<std::vector<OutboundFrame>> DispatchWorldObjectPacket(
			const std::string& a_type,
			std::uint32_t a_connectionId,
			Session& a_session,
			const Json& a_packet,
			double a_monotonicSeconds)
		{
			if (auto frames = DispatchInventoryPacket(a_type, a_connectionId, a_session, a_packet, a_monotonicSeconds)) {
				return frames;
			}
			if (a_type != "objectActivateRequest" && a_type != "objectLockRequest") return std::nullopt;
			return HandleObjectStateRequest(a_connectionId, a_session, a_packet, a_monotonicSeconds);
		}
