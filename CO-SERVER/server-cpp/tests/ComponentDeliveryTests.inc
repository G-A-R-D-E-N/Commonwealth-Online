	std::vector<OutboundFrame> ReliableTo(std::vector<OutboundFrame> a_frames, std::uint32_t a_connection = 17)
	{
		std::erase_if(a_frames, [a_connection](const auto& frame) { return frame.connectionId != a_connection || frame.delivery != Delivery::ReliableOrdered || RawPacketType(frame.payload); });
		return a_frames;
	}

	F4T::Replication::DecodedFrame ReadComponent(const OutboundFrame& a_frame)
	{
		F4T::Replication::DecodedFrame decoded;
		assert(F4T::Replication::DecodeFrame(a_frame.payload, decoded, [](auto) { return true; }) == F4T::Replication::DecodeStatus::Valid);
		return decoded;
	}

	void AcceptComponent(F4T::Replication::SnapshotReceiver& a_receiver, const OutboundFrame& a_frame)
	{
		const auto decoded = ReadComponent(a_frame);
		if (decoded.kind == F4T::Replication::FrameKind::Baseline) assert(a_receiver.AcceptBaseline(decoded.baseline, decoded.snapshotSequence));
		else assert(a_receiver.AcceptDelta(decoded.delta, decoded.snapshotSequence, decoded.baselineSequence));
	}

	void TestComponentSendRecovery()
	{
		using namespace F4T::Replication;
		ProtocolHost host;
		ComponentRegistry registry;
		assert(registry.RegisterDefinition(Family::DroppedItem, AuthorityClass::Server, Channel::Reliable));
		assert(registry.RegisterDefinition(Family::Transform, AuthorityClass::Server, Channel::Unreliable));
		assert(host.SetComponentDefinitions(*registry.Freeze()));
		std::vector<std::uint32_t> players;
		for (std::uint32_t connection : { 17u, 18u }) {
			const auto welcome = host.HandleConnected(connection, 1000.0);
			players.push_back(Json::parse(welcome.front().payload).at("playerId").get<std::uint32_t>());
			(void)host.HandleMessage(connection, JsonBytes(Json{ { "type", "hello" }, { "protocolVersion", 2 },
				{ "capabilities", Json::array({ "component-snapshot-v1", "component-snapshot-channels-v1" }) } }), 1.0, 1000.0);
			(void)host.HandleMessage(connection, JsonBytes(Json{ { "type", "transform" }, { "cellId", "CAFE" },
				{ "worldspaceId", "F00D" }, { "x", 1.0 }, { "y", 2.0 }, { "z", 3.0 },
				{ "angleZ", 0.0 }, { "movementType", "spawn" } }), 2.0, 1000.0);
		}
		DirectComponentPublicationContract contract;
		for (const auto player : players) contract.peers.push_back({ player, true, true, { Family::DroppedItem, Family::Transform } });
		assert(host.SetDirectComponentPublicationContract(std::move(contract)));
		SnapshotState state;
		state[{ players.at(1), Family::DroppedItem }] = { 1 };
		auto frames = ReliableTo(host.PublishComponentSnapshot(state));
		assert(frames.size() == 1 && frames.front().connectionId == 17);
		sendResult = CO_GNS_SEND_BACKPRESSURE;
		SendFrames(nullptr, host, frames, nullptr, nullptr, 3.0, 1000.0, nullptr);
		auto retry = ReliableTo(host.PublishComponentSnapshot(state));
		assert(retry.size() == 1);
		assert(ReadComponent(retry.front()).kind == FrameKind::Baseline);
		sendResult = CO_GNS_SEND_SENT;
		SendFrames(nullptr, host, retry, nullptr, nullptr, 3.0, 1000.0, nullptr);
		SnapshotReceiver receiver;
		AcceptComponent(receiver, retry.front());
		assert(receiver.CurrentView() == state && host.RetryComponentSnapshots().empty());
		state[{ players.at(1), Family::DroppedItem }] = { 2 };
		frames = ReliableTo(host.PublishComponentSnapshot(state));
		assert(frames.size() == 1 && ReadComponent(frames.front()).kind == FrameKind::Delta);
		sendResult = CO_GNS_SEND_BACKPRESSURE;
		SendFrames(nullptr, host, frames, nullptr, nullptr, 4.0, 1000.0, nullptr);
		retry = host.RetryComponentSnapshots();
		assert(retry.size() == 1 && ReadComponent(retry.front()).baseline == state);
		SendFrames(nullptr, host, retry, nullptr, nullptr, 4.5, 1000.0, nullptr);
		const auto failedSequence = ReadComponent(retry.front()).snapshotSequence;
		retry = host.RetryComponentSnapshots();
		assert(retry.size() == 1 && F4T::GnsSnapshotEnvelope::IsNewer(ReadComponent(retry.front()).snapshotSequence, failedSequence));
		sendResult = CO_GNS_SEND_SENT;
		SendFrames(nullptr, host, retry, nullptr, nullptr, 5.0, 1000.0, nullptr);
		AcceptComponent(receiver, retry.front());
		assert(receiver.CurrentView() == state && host.RetryComponentSnapshots().empty());
		state[{ players.at(1), Family::DroppedItem }] = { 5 };
		frames = ReliableTo(host.PublishComponentSnapshot(state));
		sendResult = CO_GNS_SEND_BACKPRESSURE;
		SendFrames(nullptr, host, frames, nullptr, nullptr, 10.0, 1000.0, nullptr);
		const auto moveRecipient = [&](std::string_view a_cell, double a_time) {
			(void)host.HandleMessage(17, JsonBytes(Json{ { "type", "transform" }, { "cellId", a_cell },
				{ "worldspaceId", a_cell == "CAFE" ? "F00D" : "DEAD" }, { "x", 1.0 }, { "y", 2.0 }, { "z", 3.0 },
				{ "angleZ", 0.0 }, { "movementType", "teleport" } }), a_time, 1000.0);
		};
		moveRecipient("BEEF", 20.0);
		retry = host.RetryComponentSnapshots();
		assert(retry.size() == 1 && ReadComponent(retry.front()).baseline.empty());
		SendFrames(nullptr, host, retry, nullptr, nullptr, 20.5, 1000.0, nullptr);
		moveRecipient("CAFE", 30.0);
		retry = host.RetryComponentSnapshots();
		assert(retry.size() == 1 && ReadComponent(retry.front()).baseline == state);
		sendResult = CO_GNS_SEND_SENT;
		SendFrames(nullptr, host, retry, nullptr, nullptr, 30.5, 1000.0, nullptr);
		AcceptComponent(receiver, retry.front());
		assert(receiver.CurrentView() == state && host.RetryComponentSnapshots().empty());
		state.clear();
		frames = ReliableTo(host.PublishComponentSnapshot(state));
		assert(frames.size() == 1 && ReadComponent(frames.front()).kind == FrameKind::Delta);
		sendResult = CO_GNS_SEND_BACKPRESSURE;
		SendFrames(nullptr, host, frames, nullptr, nullptr, 6.0, 1000.0, nullptr);
		retry = host.RetryComponentSnapshots();
		assert(retry.size() == 1 && ReadComponent(retry.front()).kind == FrameKind::Baseline && ReadComponent(retry.front()).baseline.empty());
		sendResult = CO_GNS_SEND_SENT;
		SendFrames(nullptr, host, retry, nullptr, nullptr, 6.5, 1000.0, nullptr);
		AcceptComponent(receiver, retry.front());
		assert(receiver.CurrentView().empty() && host.RetryComponentSnapshots().empty());
		frames = { OutboundFrame{ 17, Delivery::ReliableOrdered, JsonBytes(Json{ { "type", "worldState" } }) } };
		sendResult = CO_GNS_SEND_BACKPRESSURE;
		SendFrames(nullptr, host, frames, nullptr, nullptr, 7.0, 1000.0, nullptr);
		assert(host.RetryComponentSnapshots().empty());
		state[{ players.at(1), Family::DroppedItem }] = { 3 };
		state[{ players.at(1), Family::Transform }] = { 4 };
		auto combined = host.PublishComponentSnapshot(state);
		frames = ReliableTo(combined);
		SendFrames(nullptr, host, frames, nullptr, nullptr, 8.0, 1000.0, nullptr);
		std::erase_if(combined, [](const auto& frame) { return frame.connectionId != 17 || frame.delivery != Delivery::UnreliableSequenced; });
		assert(combined.size() == 1);
		sendResult = CO_GNS_SEND_SENT;
		SendFrames(nullptr, host, combined, nullptr, nullptr, 8.0, 1000.0, nullptr);
		retry = host.RetryComponentSnapshots();
		assert(retry.size() == 1 && retry.front().connectionId == 17 && retry.front().delivery == Delivery::ReliableOrdered);
		(void)host.HandleDisconnected(17, 1001.0);
		assert(host.RetryComponentSnapshots().empty());
	}

	void TestCompatibilitySendRecovery()
	{
		using namespace F4T::Replication;
		using namespace F4T::Compatibility;
		ProtocolHost host;
		ComponentRegistry registry;
		assert(registry.RegisterDefinition(Family::DroppedItem, AuthorityClass::Server, Channel::Reliable));
		assert(host.SetComponentDefinitions(*registry.Freeze()));
		FamilyAdapter adapter;
		adapter.metadata = { { "testState" }, { Family::DroppedItem }, AllConversionDirections, "component-test-state-v1" };
		adapter.legacyToCanonical = [](std::string_view a_text) -> std::optional<CanonicalValue> {
			const auto value = Json::parse(a_text, nullptr, false);
			if (!value.is_number_unsigned() || value.get<std::uint64_t>() > 255) return std::nullopt;
			return CanonicalValue{ value.get<std::uint8_t>() };
		};
		adapter.canonicalToLegacy = [](const CanonicalValue& a_value) -> std::optional<std::string> {
			const auto value = std::any_cast<std::uint8_t>(&a_value);
			return value ? std::optional{ std::to_string(*value) } : std::nullopt;
		};
		adapter.canonicalToComponent = [](const CanonicalValue& a_value) -> std::optional<std::vector<std::uint8_t>> {
			const auto value = std::any_cast<std::uint8_t>(&a_value);
			return value ? std::optional{ std::vector<std::uint8_t>{ *value } } : std::nullopt;
		};
		adapter.componentToCanonical = [](std::span<const std::uint8_t> a_bytes) -> std::optional<CanonicalValue> {
			return a_bytes.size() == 1 ? std::optional{ CanonicalValue{ a_bytes.front() } } : std::nullopt;
		};
		assert(host.RegisterCompatibilityAdapter(std::move(adapter)));
		std::vector<std::uint32_t> players;
		for (std::uint32_t connection : { 27u, 28u }) {
			const auto welcome = host.HandleConnected(connection, 1000.0);
			players.push_back(Json::parse(welcome.front().payload).at("playerId").get<std::uint32_t>());
			(void)host.HandleMessage(connection, JsonBytes(Json{ { "type", "hello" }, { "protocolVersion", 2 },
				{ "capabilities", Json::array({ "component-snapshot-v1", "component-snapshot-channels-v1", "component-test-state-v1" }) } }), 1.0, 1000.0);
			(void)host.HandleMessage(connection, JsonBytes(Json{ { "type", "transform" }, { "cellId", "CAFE" },
				{ "worldspaceId", "F00D" }, { "x", 1.0 }, { "y", 2.0 }, { "z", 3.0 },
				{ "angleZ", 0.0 }, { "movementType", "spawn" } }), 2.0, 1000.0);
		}
		CompatiblePublication publication{ players.at(1), Family::DroppedItem, "testState", 1,
			CanonicalValue{ std::uint8_t{ 9 } }, { players.front() }, {} };
		auto result = host.PublishCompatible(publication);
		assert(result.status == CompatibilityPublishStatus::Accepted && result.frames.size() == 1);
		sendResult = CO_GNS_SEND_BACKPRESSURE;
		SendFrames(nullptr, host, result.frames, nullptr, nullptr, 3.0, 1000.0, nullptr);
		auto retry = host.RetryComponentSnapshots();
		assert(retry.size() == 1 && retry.front().connectionId == 27 && ReadComponent(retry.front()).kind == FrameKind::Baseline);
		SnapshotState expected;
		expected[{ players.at(1), Family::DroppedItem }] = { 9 };
		assert(ReadComponent(retry.front()).baseline == expected);
		sendResult = CO_GNS_SEND_SENT;
		SendFrames(nullptr, host, retry, nullptr, nullptr, 3.5, 1000.0, nullptr);
		SnapshotReceiver receiver;
		AcceptComponent(receiver, retry.front());
		assert(receiver.CurrentView() == expected && host.RetryComponentSnapshots().empty());
		auto removal = host.RemoveCompatibilityEntity(players.at(1));
		assert(removal.size() == 1 && ReadComponent(removal.front()).kind == FrameKind::Delta);
		sendResult = CO_GNS_SEND_BACKPRESSURE;
		SendFrames(nullptr, host, removal, nullptr, nullptr, 4.0, 1000.0, nullptr);
		retry = host.RetryComponentSnapshots();
		assert(retry.size() == 1 && ReadComponent(retry.front()).baseline.empty());
		sendResult = CO_GNS_SEND_SENT;
		SendFrames(nullptr, host, retry, nullptr, nullptr, 4.5, 1000.0, nullptr);
		AcceptComponent(receiver, retry.front());
		assert(receiver.CurrentView().empty() && host.RetryComponentSnapshots().empty());
	}
