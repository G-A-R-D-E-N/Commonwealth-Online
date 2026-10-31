#include "ServerProtocol.h" // requires public protocol API

#include <algorithm> // requires test utilities
#include <cassert> // requires test assertions
#include <cstdint> // requires test ids
#include <initializer_list> // requires state fixtures
#include <map> // requires sequence fixtures
#include <nlohmann/json.hpp> // requires legacy packet fixtures
#include <string> // requires position fixtures
#include <string_view> // requires packet helpers
#include <utility> // requires move operations
#include <vector> // requires state fixtures

namespace
{
	using Json = nlohmann::json;
	using CommonwealthOnline::Server::Delivery;
	using CommonwealthOnline::Server::DirectComponentPublicationContract;
	using CommonwealthOnline::Server::DirectComponentPublicationPeer;
	using CommonwealthOnline::Server::OutboundFrame;
	using CommonwealthOnline::Server::ProtocolHost;
	using F4T::GnsSnapshotEnvelope::DecodeResult;
	using F4T::GnsSnapshotEnvelope::Family;
	using F4T::Replication::ComponentKey;
	using F4T::Replication::DecodedFrame;
	using F4T::Replication::DecodeFrame;
	using F4T::Replication::DecodeStatus;
	using F4T::Replication::SnapshotReceiver;
	using F4T::Replication::SnapshotState;

	F4T::Replication::FrozenComponentDefinitionTable Definitions()
	{
		F4T::Replication::ComponentRegistry registry;
		assert(registry.RegisterDefinition(Family::Transform, F4T::Replication::AuthorityClass::Server, F4T::Replication::Channel::Unreliable));
		assert(registry.RegisterDefinition(Family::NpcState, F4T::Replication::AuthorityClass::Server, F4T::Replication::Channel::Unreliable));
		const auto frozen = registry.Freeze();
		assert(frozen.has_value());
		return *frozen;
	}

	struct Position
	{
		std::string cell;
		std::string world;
		double x{};
		double y{};
	};

	std::vector<std::uint8_t> EncodeJson(const Json& a_packet)
	{
		const auto text = a_packet.dump();
		return { text.begin(), text.end() };
	}

	std::vector<std::uint8_t> EncodeSnapshot(const Json& a_packet, Family a_family, std::uint32_t a_sequence)
	{
		const auto json = a_packet.dump();
		std::vector<std::uint8_t> output(F4T::GnsSnapshotEnvelope::HeaderSize + json.size());
		assert(F4T::GnsSnapshotEnvelope::WriteHeader(std::span<std::uint8_t>{ output }, a_family, a_sequence));
		std::copy(json.begin(), json.end(), output.begin() + static_cast<std::ptrdiff_t>(F4T::GnsSnapshotEnvelope::HeaderSize));
		return output;
	}

	SnapshotState State(std::initializer_list<std::pair<std::uint32_t, std::uint8_t>> a_values)
	{
		SnapshotState state;
		for (const auto [entityId, value] : a_values) {
			state[ComponentKey{ entityId, Family::Transform }] = { value };
		}
		return state;
	}

	SnapshotState StateWithBytes(std::uint32_t a_entityId, std::vector<std::uint8_t> a_bytes)
	{
		SnapshotState state;
		state[ComponentKey{ a_entityId, Family::Transform }] = std::move(a_bytes);
		return state;
	}

	DecodedFrame DecodeComponent(const OutboundFrame& a_frame)
	{
		assert(a_frame.delivery == Delivery::UnreliableSequenced);
		DecodedFrame decoded;
		const auto status = DecodeFrame(
			a_frame.payload,
			decoded,
			[](Family a_family) { return a_family == Family::Transform || a_family == Family::NpcState; });
		assert(status == DecodeStatus::Valid);
		return decoded;
	}

	std::size_t CountFramesTo(const std::vector<OutboundFrame>& a_frames, std::uint32_t a_connectionId)
	{
		return static_cast<std::size_t>(std::count_if(
			a_frames.begin(),
			a_frames.end(),
			[a_connectionId](const auto& a_frame) { return a_frame.connectionId == a_connectionId; }));
	}

	const OutboundFrame& OnlyFrameTo(const std::vector<OutboundFrame>& a_frames, std::uint32_t a_connectionId)
	{
		const OutboundFrame* result = nullptr;
		for (const auto& frame : a_frames) {
			if (frame.connectionId != a_connectionId) {
				continue;
			}
			assert(result == nullptr);
			result = std::addressof(frame);
		}
		assert(result != nullptr);
		return *result;
	}

	bool HasLegacyTransformTo(const std::vector<OutboundFrame>& a_frames, std::uint32_t a_connectionId)
	{
		return std::any_of(a_frames.begin(), a_frames.end(), [a_connectionId](const auto& a_frame) {
			if (a_frame.connectionId != a_connectionId) {
				return false;
			}
			F4T::GnsSnapshotEnvelope::Header header{};
			return F4T::GnsSnapshotEnvelope::Decode(a_frame.payload, header) == DecodeResult::Valid &&
				header.family == Family::Transform;
		});
	}

	struct Harness
	{
		ProtocolHost host{ "Snapshot Envelope Test" };
		std::uint32_t nextConnection{ 1 };
		double clock{ 100.0 };
		std::map<std::uint32_t, std::uint32_t> transformSequences;
		std::vector<std::uint32_t> directPeers;

		Harness() { assert(host.SetComponentDefinitions(Definitions())); }

		double NextTime()
		{
			clock += 2.0;
			return clock;
		}

		std::uint32_t Connect(std::uint32_t& a_playerId)
		{
			const auto connectionId = nextConnection++;
			const auto welcome = host.HandleConnected(connectionId, NextTime());
			assert(welcome.size() == 1);
			const auto packet = Json::parse(std::string{
				reinterpret_cast<const char*>(welcome.front().payload.data()),
				welcome.front().payload.size() });
			a_playerId = packet.at("playerId").get<std::uint32_t>();
			return connectionId;
		}

		void Activate(std::uint32_t a_connectionId, std::uint32_t a_playerId)
		{
			const auto frames = host.HandleMessage(
				a_connectionId,
				EncodeJson(Json{ { "type", "hello" }, { "protocolVersion", 2 }, { "capabilities", Json::array({ "component-snapshot-v1", "component-snapshot-channels-v1" }) } }),
				NextTime(),
				NextTime());
			assert(std::any_of(frames.begin(), frames.end(), [a_playerId](const auto& a_frame) {
				if (a_frame.payload.empty()) {
					return false;
				}
				try {
					const auto packet = Json::parse(std::string{
						reinterpret_cast<const char*>(a_frame.payload.data()),
						a_frame.payload.size() });
					return packet.value("type", std::string{}) == "sessionReady" &&
						packet.value("playerId", std::uint32_t{}) == a_playerId;
				} catch (...) {
					return false;
				}
			}));
			transformSequences[a_connectionId] = 0;
			directPeers.push_back(a_playerId);
			DirectComponentPublicationContract contract;
			for (const auto peerId : directPeers) {
				contract.peers.push_back(DirectComponentPublicationPeer{ peerId, true, false, { Family::Transform } });
			}
			assert(host.SetDirectComponentPublicationContract(std::move(contract)));
		}

		std::uint32_t ActivateNew()
		{
			std::uint32_t playerId = 0;
			const auto connectionId = Connect(playerId);
			Activate(connectionId, playerId);
			return connectionId;
		}

		std::vector<OutboundFrame> Transform(
			std::uint32_t a_connectionId,
			const Position& a_position,
			std::string_view a_movementType)
		{
			const auto sequence = ++transformSequences[a_connectionId];
			const Json packet{
				{ "type", "transform" },
				{ "cellId", a_position.cell },
				{ "worldspaceId", a_position.world },
				{ "x", a_position.x },
				{ "y", a_position.y },
				{ "z", 0.0 },
				{ "angleZ", 0.0 },
				{ "movementType", std::string{ a_movementType } }
			};
			return host.HandleMessage(
				a_connectionId,
				EncodeSnapshot(packet, Family::Transform, sequence),
				NextTime(),
				NextTime());
		}
	};

	const Position kPositionA{ "00000100", "0000F00D", 0.0, 0.0 };
	const Position kPositionB{ "00000100", "0000F00D", 100.0, 0.0 };
	const Position kPositionAway{ "00000200", "0000DEAD", 0.0, 0.0 };

	void TestPerPeerBaselinesAndFirstTransform()
	{
		Harness harness;
		std::uint32_t playerA = 0;
		std::uint32_t playerB = 0;
		std::uint32_t playerC = 0;
		const auto connectionA = harness.Connect(playerA);
		const auto connectionB = harness.Connect(playerB);
		const auto connectionC = harness.Connect(playerC);
		harness.Activate(connectionA, playerA);
		harness.Activate(connectionB, playerB);
		harness.Activate(connectionC, playerC);
		harness.Transform(connectionA, kPositionA, "spawn");
		harness.Transform(connectionB, kPositionB, "spawn");
		harness.Transform(connectionC, kPositionAway, "spawn");

		const auto frames = harness.host.PublishComponentSnapshot(State({ { playerA, 1 }, { playerB, 2 }, { playerC, 3 } }));
		assert(frames.size() == 3);
		const auto baselineA = DecodeComponent(OnlyFrameTo(frames, connectionA));
		const auto baselineB = DecodeComponent(OnlyFrameTo(frames, connectionB));
		const auto baselineC = DecodeComponent(OnlyFrameTo(frames, connectionC));
		assert(baselineA.kind == F4T::Replication::FrameKind::Baseline);
		assert(baselineB.kind == F4T::Replication::FrameKind::Baseline);
		assert(baselineC.kind == F4T::Replication::FrameKind::Baseline);
		assert(baselineA.baseline == State({ { playerB, 2 } }));
		assert(baselineB.baseline == State({ { playerA, 1 } }));
		assert(baselineC.baseline.empty());
	}

	void TestFixedBaselineDeltasSuppressionAndReturn()
	{
		Harness harness;
		std::uint32_t playerA = 0;
		std::uint32_t playerB = 0;
		const auto connectionA = harness.Connect(playerA);
		const auto connectionB = harness.Connect(playerB);
		harness.Activate(connectionA, playerA);
		harness.Activate(connectionB, playerB);
		harness.Transform(connectionA, kPositionA, "spawn");
		harness.Transform(connectionB, kPositionB, "spawn");

		const auto stateA = State({ { playerA, 10 }, { playerB, 20 } });
		const auto baselineFrames = harness.host.PublishComponentSnapshot(stateA);
		const auto baselineA = DecodeComponent(OnlyFrameTo(baselineFrames, connectionA));
		SnapshotReceiver receiver;
		assert(receiver.AcceptBaseline(baselineA.baseline, baselineA.snapshotSequence));

		const auto stateB = State({ { playerA, 10 }, { playerB, 21 } });
		const auto firstDeltaFrames = harness.host.PublishComponentSnapshot(stateB);
		assert(CountFramesTo(firstDeltaFrames, connectionA) == 1);
		assert(CountFramesTo(firstDeltaFrames, connectionB) == 0);
		const auto firstDelta = DecodeComponent(OnlyFrameTo(firstDeltaFrames, connectionA));
		assert(firstDelta.kind == F4T::Replication::FrameKind::Delta);
		assert(firstDelta.baselineSequence == baselineA.snapshotSequence);
		assert(receiver.AcceptDelta(firstDelta.delta, firstDelta.snapshotSequence, firstDelta.baselineSequence));
		assert(receiver.CurrentView() == State({ { playerB, 21 } }));

		assert(harness.host.PublishComponentSnapshot(stateB).empty());

		const auto stateC = State({ { playerA, 10 }, { playerB, 20 } });
		const auto returnFrames = harness.host.PublishComponentSnapshot(stateC);
		assert(CountFramesTo(returnFrames, connectionA) == 1);
		assert(CountFramesTo(returnFrames, connectionB) == 0);
		const auto returnDelta = DecodeComponent(OnlyFrameTo(returnFrames, connectionA));
		assert(returnDelta.kind == F4T::Replication::FrameKind::Delta);
		assert(returnDelta.delta.Empty());
		assert(returnDelta.baselineSequence == baselineA.snapshotSequence);
		assert(receiver.AcceptDelta(returnDelta.delta, returnDelta.snapshotSequence, returnDelta.baselineSequence));
		assert(receiver.CurrentView() == State({ { playerB, 20 } }));

		const auto stateD = State({ { playerA, 10 }, { playerB, 22 } });
		const auto secondDeltaFrames = harness.host.PublishComponentSnapshot(stateD);
		const auto secondDelta = DecodeComponent(OnlyFrameTo(secondDeltaFrames, connectionA));
		assert(secondDelta.baselineSequence == baselineA.snapshotSequence);
		assert(receiver.AcceptDelta(secondDelta.delta, secondDelta.snapshotSequence, secondDelta.baselineSequence));
		assert(receiver.CurrentView() == State({ { playerB, 22 } }));
	}

	void TestPublicationOpportunityKeyframeAndReset()
	{
		Harness harness;
		std::uint32_t playerA = 0;
		std::uint32_t playerB = 0;
		const auto connectionA = harness.Connect(playerA);
		const auto connectionB = harness.Connect(playerB);
		harness.Activate(connectionA, playerA);
		harness.Activate(connectionB, playerB);
		harness.Transform(connectionA, kPositionA, "spawn");
		harness.Transform(connectionB, kPositionB, "spawn");

		const auto stateA = State({ { playerB, 1 } });
		const auto initialFrames = harness.host.PublishComponentSnapshot(stateA);
		const auto initialA = DecodeComponent(OnlyFrameTo(initialFrames, connectionA));
		for (std::uint32_t index = 0; index < 31; ++index) {
			assert(harness.host.PublishComponentSnapshot(stateA).empty());
		}
		const auto keyframeFrames = harness.host.PublishComponentSnapshot(stateA);
		const auto keyframeA = DecodeComponent(OnlyFrameTo(keyframeFrames, connectionA));
		assert(keyframeA.kind == F4T::Replication::FrameKind::Baseline);
		assert(keyframeA.snapshotSequence != initialA.snapshotSequence);
		assert(keyframeA.baseline == initialA.baseline);

		const auto changedFrames = harness.host.PublishComponentSnapshot(State({ { playerB, 2 } }));
		const auto changedA = DecodeComponent(OnlyFrameTo(changedFrames, connectionA));
		assert(changedA.kind == F4T::Replication::FrameKind::Delta);
		assert(changedA.baselineSequence == keyframeA.snapshotSequence);
	}

	void TestBoundaryAndDisconnectCleanup()
	{
		Harness harness;
		std::uint32_t playerA = 0;
		std::uint32_t playerB = 0;
		const auto connectionA = harness.Connect(playerA);
		const auto connectionB = harness.Connect(playerB);
		harness.Activate(connectionA, playerA);
		harness.Activate(connectionB, playerB);
		harness.Transform(connectionA, kPositionA, "spawn");
		harness.Transform(connectionB, kPositionB, "spawn");
		const auto state = State({ { playerA, 1 }, { playerB, 2 } });
		const auto baselineFrames = harness.host.PublishComponentSnapshot(state);
		(void)DecodeComponent(OnlyFrameTo(baselineFrames, connectionA));

		harness.Transform(connectionB, kPositionAway, "cell_change");
		const auto hiddenFrames = harness.host.PublishComponentSnapshot(state);
		const auto hiddenA = DecodeComponent(OnlyFrameTo(hiddenFrames, connectionA));
		assert(hiddenA.delta.removedEntities == std::vector<std::uint32_t>{ playerB });

		harness.Transform(connectionB, kPositionB, "cell_change");
		const auto restoredFrames = harness.host.PublishComponentSnapshot(state);
		const auto restoredA = DecodeComponent(OnlyFrameTo(restoredFrames, connectionA));
		assert(restoredA.delta.Empty());

		harness.host.HandleDisconnected(connectionB, harness.NextTime());
		const auto disconnectedFrames = harness.host.PublishComponentSnapshot(state);
		assert(CountFramesTo(disconnectedFrames, connectionB) == 0);
		assert(CountFramesTo(disconnectedFrames, connectionA) == 1);
		const auto disconnectedA = DecodeComponent(OnlyFrameTo(disconnectedFrames, connectionA));
		assert(disconnectedA.delta.removedEntities == std::vector<std::uint32_t>{ playerB });
	}

	void TestInactiveSessionLifecycle()
	{
		Harness harness;
		std::uint32_t playerA = 0;
		std::uint32_t playerB = 0;
		const auto connectionA = harness.Connect(playerA);
		const auto connectionB = harness.Connect(playerB);
		harness.Activate(connectionA, playerA);
		const auto inactiveFrames = harness.host.PublishComponentSnapshot(State({ { playerA, 1 }, { playerB, 2 } }));
		assert(CountFramesTo(inactiveFrames, connectionA) == 1);
		assert(CountFramesTo(inactiveFrames, connectionB) == 0);

		harness.Activate(connectionB, playerB);
		const auto activeFrames = harness.host.PublishComponentSnapshot(State({ { playerA, 1 }, { playerB, 2 } }));
		assert(CountFramesTo(activeFrames, connectionA) == 0);
		assert(CountFramesTo(activeFrames, connectionB) == 1);
		const auto baselineB = DecodeComponent(OnlyFrameTo(activeFrames, connectionB));
		assert(baselineB.kind == F4T::Replication::FrameKind::Baseline);
		assert(baselineB.baseline.empty());
	}

	void TestTransportBoundaryAndFailedCommit()
	{
		Harness harness;
		std::uint32_t playerA = 0;
		std::uint32_t playerB = 0;
		const auto connectionA = harness.Connect(playerA);
		const auto connectionB = harness.Connect(playerB);
		harness.Activate(connectionA, playerA);
		harness.Activate(connectionB, playerB);
		harness.Transform(connectionA, kPositionA, "spawn");
		harness.Transform(connectionB, kPositionB, "spawn");

		const auto exactState = StateWithBytes(playerB, std::vector<std::uint8_t>(65514, static_cast<std::uint8_t>(7)));
		const auto exactFrames = harness.host.PublishComponentSnapshot(exactState);
		const auto& exactFrame = OnlyFrameTo(exactFrames, connectionA);
		assert(exactFrame.payload.size() == 64U * 1024U);

		const auto fallbackState = StateWithBytes(playerB, std::vector<std::uint8_t>(65514, static_cast<std::uint8_t>(8)));
		const auto fallbackFrames = harness.host.PublishComponentSnapshot(fallbackState);
		const auto fallbackA = DecodeComponent(OnlyFrameTo(fallbackFrames, connectionA));
		assert(fallbackA.kind == F4T::Replication::FrameKind::Baseline);
		assert(fallbackA.baseline == fallbackState);

		const auto oversizedState = StateWithBytes(playerB, std::vector<std::uint8_t>(65515, static_cast<std::uint8_t>(8)));
		const auto oversizedFrames = harness.host.PublishComponentSnapshot(oversizedState);
		assert(CountFramesTo(oversizedFrames, connectionA) == 0);
		const auto retryFallbackFrames = harness.host.PublishComponentSnapshot(fallbackState);
		const auto retryFallbackA = DecodeComponent(OnlyFrameTo(retryFallbackFrames, connectionA));
		assert(retryFallbackA.kind == F4T::Replication::FrameKind::Baseline);
		assert(retryFallbackA.baseline == fallbackState);
	}

	void TestDueOversizedKeyframeRetries()
	{
		Harness harness;
		std::uint32_t playerA = 0;
		std::uint32_t playerB = 0;
		const auto connectionA = harness.Connect(playerA);
		const auto connectionB = harness.Connect(playerB);
		harness.Activate(connectionA, playerA);
		harness.Activate(connectionB, playerB);
		harness.Transform(connectionA, kPositionA, "spawn");
		harness.Transform(connectionB, kPositionB, "spawn");

		const auto smallState = StateWithBytes(playerB, std::vector<std::uint8_t>(100, static_cast<std::uint8_t>(1)));
		const auto initialFrames = harness.host.PublishComponentSnapshot(smallState);
		(void)OnlyFrameTo(initialFrames, connectionA);
		const auto oversizedState = StateWithBytes(playerB, std::vector<std::uint8_t>(65535, static_cast<std::uint8_t>(2)));
		for (std::uint32_t index = 0; index < 32; ++index) {
			const auto frames = harness.host.PublishComponentSnapshot(oversizedState);
			assert(CountFramesTo(frames, connectionA) == 0);
		}
		assert(CountFramesTo(harness.host.PublishComponentSnapshot(oversizedState), connectionA) == 0);

		const auto retryFrames = harness.host.PublishComponentSnapshot(smallState);
		const auto retryA = DecodeComponent(OnlyFrameTo(retryFrames, connectionA));
		assert(retryA.kind == F4T::Replication::FrameKind::Baseline);
		assert(retryA.baseline == smallState);
	}

	void TestLegacyRoutingRemainsAvailable()
	{
		Harness harness;
		std::uint32_t playerA = 0;
		std::uint32_t playerB = 0;
		const auto connectionA = harness.Connect(playerA);
		const auto connectionB = harness.Connect(playerB);
		harness.Activate(connectionA, playerA);
		harness.Activate(connectionB, playerB);
		harness.Transform(connectionA, kPositionA, "spawn");
		const auto before = harness.Transform(connectionB, kPositionB, "spawn");
		assert(HasLegacyTransformTo(before, connectionA));
		harness.host.PublishComponentSnapshot(State({ { playerA, 1 }, { playerB, 2 } }));
		const auto after = harness.Transform(connectionA, kPositionA, "normal");
		assert(HasLegacyTransformTo(after, connectionB));
	}
}

int main()
{
	TestPerPeerBaselinesAndFirstTransform();
	TestFixedBaselineDeltasSuppressionAndReturn();
	TestPublicationOpportunityKeyframeAndReset();
	TestBoundaryAndDisconnectCleanup();
	TestInactiveSessionLifecycle();
	TestTransportBoundaryAndFailedCommit();
	TestDueOversizedKeyframeRetries();
	TestLegacyRoutingRemainsAvailable();
	return 0;
}
