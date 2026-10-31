#include "ServerProtocol.h"

#include "F4TCompatibilityShim.h"
#include "F4TGnsSnapshotEnvelope.h"
#include "F4TSnapshotDelta.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace
{
	using Json = nlohmann::json;
	using CommonwealthOnline::Server::CompatiblePublication;
	using CommonwealthOnline::Server::CompatibilityPublishStatus;
	using CommonwealthOnline::Server::Delivery;
	using CommonwealthOnline::Server::DirectComponentPublicationContract;
	using CommonwealthOnline::Server::DirectComponentPublicationPeer;
	using CommonwealthOnline::Server::OutboundFrame;
	using CommonwealthOnline::Server::ProtocolHost;
	using F4T::Compatibility::CanonicalValue;
	using F4T::Compatibility::FamilyAdapter;
	using F4T::Compatibility::AllConversionDirections;
	using F4T::GnsSnapshotEnvelope::DecodeResult;
	using F4T::Replication::ComponentKey;
	using F4T::Replication::DecodeFrame;
	using F4T::Replication::DecodeStatus;
	using F4T::Replication::DecodedFrame;
	using F4T::Replication::EncodeBaseline;
	using F4T::Replication::Family;
	using F4T::Replication::FrameKind;
	using F4T::Replication::SnapshotState;

	constexpr Family SyntheticA = static_cast<Family>(240);
	constexpr Family SyntheticB = static_cast<Family>(241);
	constexpr Family SyntheticOneWay = static_cast<Family>(242);
	constexpr Family SyntheticReliable = static_cast<Family>(243);
	void Configure(ProtocolHost& a_host) { F4T::Replication::ComponentRegistry registry; assert(registry.RegisterDefinition(Family::Transform, F4T::Replication::AuthorityClass::Server, F4T::Replication::Channel::Unreliable)); assert(registry.RegisterDefinition(SyntheticA, F4T::Replication::AuthorityClass::Server, F4T::Replication::Channel::Unreliable)); assert(registry.RegisterDefinition(SyntheticB, F4T::Replication::AuthorityClass::Server, F4T::Replication::Channel::Unreliable)); assert(registry.RegisterDefinition(SyntheticOneWay, F4T::Replication::AuthorityClass::Server, F4T::Replication::Channel::Unreliable)); assert(registry.RegisterDefinition(SyntheticReliable, F4T::Replication::AuthorityClass::Server, F4T::Replication::Channel::Reliable)); const auto frozen = registry.Freeze(); assert(frozen.has_value() && a_host.SetComponentDefinitions(*frozen)); }

	struct SyntheticValue
	{
		std::uint32_t value{};
	};

	std::vector<std::uint8_t> Bytes(std::string_view a_text)
	{
		return { a_text.begin(), a_text.end() };
	}

	std::vector<std::uint8_t> ComponentBytes(std::uint32_t a_value)
	{
		return {
			static_cast<std::uint8_t>((a_value >> 24U) & 0xffU),
			static_cast<std::uint8_t>((a_value >> 16U) & 0xffU),
			static_cast<std::uint8_t>((a_value >> 8U) & 0xffU),
			static_cast<std::uint8_t>(a_value & 0xffU)
		};
	}

	std::optional<std::uint32_t> DecodeComponentBytes(std::span<const std::uint8_t> a_bytes)
	{
		if (a_bytes.size() != 4) {
			return std::nullopt;
		}
		return (static_cast<std::uint32_t>(a_bytes[0]) << 24U) |
		       (static_cast<std::uint32_t>(a_bytes[1]) << 16U) |
		       (static_cast<std::uint32_t>(a_bytes[2]) << 8U) |
		       static_cast<std::uint32_t>(a_bytes[3]);
	}

	FamilyAdapter MakeAdapter(
		Family a_family,
		std::string a_legacyName,
		std::string a_capability,
		std::shared_ptr<bool> a_failProjection = {})
	{
		FamilyAdapter adapter;
		adapter.metadata.legacy.name = std::move(a_legacyName);
		adapter.metadata.component.family = a_family;
		adapter.metadata.supportedDirections = AllConversionDirections;
		adapter.metadata.requiredComponentCapability = std::move(a_capability);
		adapter.legacyToCanonical = [](std::string_view a_payload) -> std::optional<CanonicalValue> {
			constexpr std::string_view prefix = "synthetic:";
			if (!a_payload.starts_with(prefix)) {
				return std::nullopt;
			}
			try {
				return CanonicalValue{ SyntheticValue{ static_cast<std::uint32_t>(std::stoul(std::string{ a_payload.substr(prefix.size()) })) } };
			} catch (...) {
				return std::nullopt;
			}
		};
		adapter.canonicalToLegacy = [](const CanonicalValue& a_value) -> std::optional<std::string> {
			const auto* value = std::any_cast<SyntheticValue>(&a_value);
			return value ? std::optional<std::string>{ "synthetic:" + std::to_string(value->value) } : std::nullopt;
		};
		adapter.canonicalToComponent = [a_failProjection](const CanonicalValue& a_value) -> std::optional<std::vector<std::uint8_t>> {
			if (a_failProjection && *a_failProjection) {
				return std::nullopt;
			}
			const auto* value = std::any_cast<SyntheticValue>(&a_value);
			return value ? std::optional{ ComponentBytes(value->value) } : std::nullopt;
		};
		adapter.componentToCanonical = [](std::span<const std::uint8_t> a_bytes) -> std::optional<CanonicalValue> {
			const auto value = DecodeComponentBytes(a_bytes);
			return value ? std::optional<CanonicalValue>{ SyntheticValue{ *value } } : std::nullopt;
		};
		return adapter;
	}

	FamilyAdapter MakeOneWayAdapter()
	{
		auto adapter = MakeAdapter(SyntheticOneWay, "syntheticOneWay", "component-synthetic-one-way-v1");
		adapter.metadata.supportedDirections = F4T::Compatibility::LegacyToComponentDirection;
		adapter.canonicalToLegacy = {};
		adapter.componentToCanonical = {};
		return adapter;
	}

	std::vector<std::uint8_t> EncodeJson(const Json& a_packet)
	{
		const auto text = a_packet.dump();
		return { text.begin(), text.end() };
	}

	std::vector<std::uint8_t> EncodeLegacySnapshot(const Json& a_packet, Family a_family, std::uint32_t a_sequence)
	{
		const auto json = a_packet.dump();
		std::vector<std::uint8_t> output(F4T::GnsSnapshotEnvelope::HeaderSize + json.size());
		assert(F4T::GnsSnapshotEnvelope::WriteHeader(output, a_family, a_sequence));
		std::copy(json.begin(), json.end(), output.begin() + static_cast<std::ptrdiff_t>(F4T::GnsSnapshotEnvelope::HeaderSize));
		return output;
	}

	Json DecodeLegacyPacket(const OutboundFrame& a_frame)
	{
		F4T::GnsSnapshotEnvelope::Header header{};
		auto payload = std::span<const std::uint8_t>{ a_frame.payload };
		const auto decoded = F4T::GnsSnapshotEnvelope::Decode(payload, header);
		if (decoded == DecodeResult::Valid) {
			payload = payload.subspan(F4T::GnsSnapshotEnvelope::HeaderSize);
		} else {
			assert(decoded == DecodeResult::NotEnvelope);
		}
		return Json::parse(std::string{ reinterpret_cast<const char*>(payload.data()), payload.size() });
	}

	std::optional<DecodedFrame> DecodeComponentFrame(const OutboundFrame& a_frame)
	{
		if (a_frame.delivery != Delivery::UnreliableSequenced && a_frame.delivery != Delivery::ReliableOrdered) {
			return std::nullopt;
		}
		DecodedFrame decoded;
		const auto status = DecodeFrame(a_frame.payload, decoded, [](Family a_family) {
			return a_family == SyntheticA || a_family == SyntheticB || a_family == SyntheticOneWay || a_family == SyntheticReliable || a_family == Family::Transform;
		});
		return status == DecodeStatus::Valid ? std::optional{ std::move(decoded) } : std::nullopt;
	}

	std::vector<std::pair<OutboundFrame, DecodedFrame>> ComponentFramesTo(
		const std::vector<OutboundFrame>& a_frames,
		std::uint32_t a_connectionId)
	{
		std::vector<std::pair<OutboundFrame, DecodedFrame>> result;
		for (const auto& frame : a_frames) {
			if (frame.connectionId != a_connectionId) {
				continue;
			}
			if (auto decoded = DecodeComponentFrame(frame)) {
				result.emplace_back(frame, std::move(*decoded));
			}
		}
		return result;
	}

	bool HasLegacyTextTo(
		const std::vector<OutboundFrame>& a_frames,
		std::uint32_t a_connectionId,
		std::string_view a_text)
	{
		for (const auto& frame : a_frames) {
			if (frame.connectionId == a_connectionId && frame.delivery == Delivery::ReliableOrdered &&
				std::string_view{ reinterpret_cast<const char*>(frame.payload.data()), frame.payload.size() } == a_text) {
				return true;
			}
		}
		return false;
	}

	std::optional<OutboundFrame> LegacyEmitter(std::uint32_t a_connectionId, std::string_view a_payload)
	{
		return OutboundFrame{ a_connectionId, Delivery::ReliableOrdered, Bytes(a_payload) };
	}

	struct Peer
	{
		std::uint32_t connectionId{};
		std::uint32_t playerId{};
	};

	struct Harness
	{
		ProtocolHost host = [] { ProtocolHost h{ "Compatibility Shim Test" }; Configure(h); return h; }();
		std::uint32_t nextConnection{ 10 };
		double clock{ 1000.0 };
		std::uint32_t transformSequence{};

		double Tick() { return clock += 2.0; }

		Peer Activate(std::vector<std::string> a_capabilities = {})
		{
			const auto connectionId = nextConnection++;
			const auto welcome = host.HandleConnected(connectionId, Tick());
			assert(welcome.size() == 1);
			const auto packet = DecodeLegacyPacket(welcome.front());
			const auto playerId = packet.at("playerId").get<std::uint32_t>();
			Json hello{ { "type", "hello" }, { "protocolVersion", 2 } };
			if (!a_capabilities.empty()) {
				hello["capabilities"] = std::move(a_capabilities);
			}
			const auto ready = host.HandleMessage(connectionId, EncodeJson(hello), Tick(), Tick());
			assert(!ready.empty());
			return Peer{ connectionId, playerId };
		}

		void Position(const Peer& a_peer, double a_x = 0.0, std::string a_cell = "00000100", std::string a_world = "0000F00D", std::string a_movement = "spawn")
		{
			Json packet{
				{ "type", "transform" },
				{ "cellId", std::move(a_cell) },
				{ "worldspaceId", std::move(a_world) },
				{ "x", a_x },
				{ "y", 0.0 },
				{ "z", 0.0 },
				{ "angleZ", 0.0 },
				{ "movementType", std::move(a_movement) }
			};
			const auto frame = EncodeLegacySnapshot(packet, Family::Transform, ++transformSequence);
			(void)host.HandleMessage(a_peer.connectionId, frame, Tick(), Tick());
		}
	};

	CompatiblePublication Publication(
		std::uint32_t a_entityId,
		Family a_family,
		std::string a_legacyFamily,
		std::uint64_t a_revision,
		std::uint32_t a_value,
		std::vector<std::uint32_t> a_recipients)
	{
		return CompatiblePublication{
			a_entityId,
			a_family,
			std::move(a_legacyFamily),
			a_revision,
			CanonicalValue{ SyntheticValue{ a_value } },
			std::move(a_recipients),
			LegacyEmitter
		};
	}

	void TestCapabilityValidationAndRegistration()
	{
		{
			ProtocolHost host;
			const auto welcome = host.HandleConnected(1, 1.0);
			const auto welcomePacket = DecodeLegacyPacket(welcome.front());
			for (const auto& capability : welcomePacket.at("capabilities")) {
				assert(!capability.get<std::string>().starts_with("component-"));
			}
			const auto ready = host.HandleMessage(1, EncodeJson(Json{ { "type", "hello" }, { "protocolVersion", 2 } }), 2.0, 2.0);
			assert(!ready.empty());
			assert(!host.RegisterCompatibilityAdapter(MakeAdapter(SyntheticA, "syntheticA", "component-synthetic-a-v1")));
		}
		{
			ProtocolHost host;
			(void)host.HandleConnected(1, 1.0);
			const auto ready = host.HandleMessage(1, EncodeJson(Json{ { "type", "hello" }, { "protocolVersion", 2 } }), 2.0, 2.0);
			assert(!ready.empty());
			(void)host.HandleDisconnected(1, 3.0);
			assert(host.ActiveCount() == 0);
			assert(!host.RegisterCompatibilityAdapter(MakeAdapter(SyntheticA, "syntheticA", "component-synthetic-a-v1")));
		}

		auto RejectHello = [](Json a_capabilities) {
			ProtocolHost host;
			(void)host.HandleConnected(1, 1.0);
			Json hello{ { "type", "hello" }, { "protocolVersion", 2 }, { "capabilities", std::move(a_capabilities) } };
			const auto before = host.Stats().packetsRejected;
			assert(host.HandleMessage(1, EncodeJson(hello), 2.0, 2.0).empty());
			assert(host.ActiveCount() == 0);
			assert(host.Stats().packetsRejected == before + 1);
		};
		RejectHello(Json("not-an-array"));
		RejectHello(Json::array({ "dup", "dup" }));
		Json tooMany = Json::array();
		for (int i = 0; i < 65; ++i) {
			tooMany.push_back("cap-" + std::to_string(i));
		}
		RejectHello(std::move(tooMany));
		RejectHello(Json::array({ std::string(65, 'x') }));
		RejectHello(Json::array({ std::string{ "bad\ncap" } }));
		RejectHello(Json::array({ 42 }));
	}

	void TestDirectPublicationModeGuard()
	{
		{
			Harness h;
			const auto a = h.Activate({ "component-snapshot-v1", "component-snapshot-channels-v1" });
			const auto b = h.Activate();
			h.Position(a, 0.0);
			h.Position(b, 100.0);
			SnapshotState state;
			state[ComponentKey{ b.playerId, Family::Transform }] = { 1, 2, 3, 4 };
			assert(h.host.PublishComponentSnapshot(state).empty());
			DirectComponentPublicationContract contract;
			contract.peers.push_back(DirectComponentPublicationPeer{ a.playerId, true, false, { Family::Transform } });
			assert(h.host.SetDirectComponentPublicationContract(std::move(contract)));
			const auto frames = h.host.PublishComponentSnapshot(state);
			assert(!ComponentFramesTo(frames, a.connectionId).empty());
		}
		{
			Harness h;
			const auto reliable = h.Activate({ "component-snapshot-v1", "component-snapshot-channels-v1" });
			const auto entity = h.Activate();
			h.Position(reliable, 0.0);
			h.Position(entity, 100.0);
			SnapshotState state;
			state[ComponentKey{ entity.playerId, SyntheticReliable }] = { 5, 6, 7, 8 };
			assert(h.host.PublishComponentSnapshot(state).empty());
			DirectComponentPublicationContract missingFamily;
			missingFamily.peers.push_back(DirectComponentPublicationPeer{ reliable.playerId, true, true, {} });
			assert(h.host.SetDirectComponentPublicationContract(std::move(missingFamily)));
			assert(h.host.PublishComponentSnapshot(state).empty());
			DirectComponentPublicationContract allowed;
			allowed.peers.push_back(DirectComponentPublicationPeer{ reliable.playerId, true, true, { SyntheticReliable } });
			assert(h.host.SetDirectComponentPublicationContract(std::move(allowed)));
			const auto frames = h.host.PublishComponentSnapshot(state);
			const auto componentFrames = ComponentFramesTo(frames, reliable.connectionId);
			const auto reliableFrame = std::find_if(componentFrames.begin(), componentFrames.end(), [](const auto& frame) {
				return frame.second.channel == F4T::Replication::Channel::Reliable;
			});
			assert(reliableFrame != componentFrames.end());
			assert(reliableFrame->first.delivery == Delivery::ReliableOrdered);
			assert(reliableFrame->second.kind == FrameKind::Baseline);
			assert(reliableFrame->second.baseline.at(ComponentKey{ entity.playerId, SyntheticReliable }) == std::vector<std::uint8_t>({ 5, 6, 7, 8 }));
		}
		{
			Harness h;
			assert(h.host.RegisterCompatibilityAdapter(MakeAdapter(SyntheticA, "syntheticA", "component-synthetic-a-v1")));
			const auto a = h.Activate({ "component-snapshot-v1", "component-synthetic-a-v1" });
			const auto b = h.Activate();
			h.Position(a, 0.0);
			h.Position(b, 100.0);
			SnapshotState state;
			state[ComponentKey{ b.playerId, Family::Transform }] = { 1, 2, 3, 4 };
			assert(h.host.PublishComponentSnapshot(state).empty());
		}
	}

	void TestOneWayLegacyToComponentAdapter()
	{
		Harness h;
		assert(h.host.RegisterCompatibilityAdapter(MakeOneWayAdapter()));
		const auto component = h.Activate({ "component-snapshot-v1", "component-synthetic-one-way-v1" });
		const auto entity = h.Activate();
		const auto legacy = h.Activate();
		h.Position(component, 0.0);
		h.Position(entity, 100.0);
		h.Position(legacy, 200.0);

		const auto result = h.host.PublishCompatible(Publication(
			entity.playerId,
			SyntheticOneWay,
			"syntheticOneWay",
			1,
			77,
			{ component.playerId, legacy.playerId }));
		assert(result.status == CompatibilityPublishStatus::Accepted);
		const auto componentFrames = ComponentFramesTo(result.frames, component.connectionId);
		assert(componentFrames.size() == 1);
		assert(componentFrames.front().second.kind == FrameKind::Baseline);
		assert(componentFrames.front().second.baseline.at(ComponentKey{ entity.playerId, SyntheticOneWay }) == ComponentBytes(77));
		assert(!HasLegacyTextTo(result.frames, legacy.connectionId, "synthetic:77"));
	}

	void TestReliableChannelNegotiation()
	{
		Harness h;
		assert(h.host.RegisterCompatibilityAdapter(MakeAdapter(SyntheticReliable, "syntheticReliable", "component-synthetic-reliable-v1")));
		const auto reliable = h.Activate({ "component-snapshot-v1", "component-snapshot-channels-v1", "component-synthetic-reliable-v1" });
		const auto legacy = h.Activate({ "component-snapshot-v1", "component-synthetic-reliable-v1" });
		const auto entity = h.Activate();
		h.Position(reliable, 0.0);
		h.Position(legacy, 100.0);
		h.Position(entity, 200.0);
		const auto result = h.host.PublishCompatible(Publication(entity.playerId, SyntheticReliable, "syntheticReliable", 1, 99, { reliable.playerId, legacy.playerId }));
		assert(result.status == CompatibilityPublishStatus::Accepted);
		const auto reliableFrames = ComponentFramesTo(result.frames, reliable.connectionId);
		assert(reliableFrames.size() == 1);
		assert(reliableFrames.front().first.delivery == Delivery::ReliableOrdered);
		assert(reliableFrames.front().second.channel == F4T::Replication::Channel::Reliable);
		assert(reliableFrames.front().second.baseline.at(ComponentKey{ entity.playerId, SyntheticReliable }) == ComponentBytes(99));
		assert(HasLegacyTextTo(result.frames, legacy.connectionId, "synthetic:99"));
	}

	void TestMixedFanoutRevisionOrderingAndDirtyRecovery()
	{
		Harness h;
		auto failA = std::make_shared<bool>(false);
		assert(h.host.RegisterCompatibilityAdapter(MakeAdapter(SyntheticA, "syntheticA", "component-synthetic-a-v1", failA)));
		assert(h.host.RegisterCompatibilityAdapter(MakeAdapter(SyntheticB, "syntheticB", "component-synthetic-b-v1")));

		const auto component = h.Activate({ "component-snapshot-v1", "component-synthetic-a-v1", "component-synthetic-b-v1" });
		const auto entity = h.Activate();
		const auto legacy = h.Activate();
		const auto late = h.Activate({ "component-snapshot-v1", "component-synthetic-a-v1", "component-synthetic-b-v1" });
		h.Position(component, 0.0);
		h.Position(entity, 100.0);
		h.Position(legacy, 200.0);
		h.Position(late, 300.0);

		const auto first = h.host.PublishCompatible(Publication(entity.playerId, SyntheticA, "syntheticA", 1, 42, { component.playerId, legacy.playerId }));
		assert(first.status == CompatibilityPublishStatus::Accepted);
		assert(HasLegacyTextTo(first.frames, legacy.connectionId, "synthetic:42"));
		assert(!HasLegacyTextTo(first.frames, component.connectionId, "synthetic:42"));
		const auto firstComponent = ComponentFramesTo(first.frames, component.connectionId);
		assert(firstComponent.size() == 1);
		assert(firstComponent.front().second.kind == FrameKind::Baseline);
		assert(firstComponent.front().second.baseline.at(ComponentKey{ entity.playerId, SyntheticA }) == ComponentBytes(42));

		const auto duplicate = h.host.PublishCompatible(Publication(entity.playerId, SyntheticA, "syntheticA", 1, 42, { late.playerId }));
		assert(duplicate.status == CompatibilityPublishStatus::Accepted);
		assert(duplicate.frames.empty());

		const auto second = h.host.PublishCompatible(Publication(entity.playerId, SyntheticA, "syntheticA", 2, 43, { component.playerId, legacy.playerId }));
		assert(second.status == CompatibilityPublishStatus::Accepted);
		assert(HasLegacyTextTo(second.frames, legacy.connectionId, "synthetic:43"));
		assert(ComponentFramesTo(second.frames, component.connectionId).size() == 1);

		const auto stale = h.host.PublishCompatible(Publication(entity.playerId, SyntheticA, "syntheticA", 1, 1, { late.playerId }));
		assert(stale.status == CompatibilityPublishStatus::Stale);
		assert(stale.frames.empty());
		const auto retryCurrent = h.host.PublishCompatible(Publication(entity.playerId, SyntheticA, "syntheticA", 2, 43, { late.playerId }));
		assert(retryCurrent.status == CompatibilityPublishStatus::Accepted);
		assert(retryCurrent.frames.empty());

		*failA = true;
		const auto failedProjection = h.host.PublishCompatible(Publication(entity.playerId, SyntheticA, "syntheticA", 3, 44, { component.playerId, legacy.playerId }));
		assert(failedProjection.status == CompatibilityPublishStatus::Accepted);
		assert(HasLegacyTextTo(failedProjection.frames, legacy.connectionId, "synthetic:44"));
		assert(ComponentFramesTo(failedProjection.frames, component.connectionId).empty());

		const auto otherFamilyBlocked = h.host.PublishCompatible(Publication(entity.playerId, SyntheticB, "syntheticB", 1, 55, { component.playerId }));
		assert(otherFamilyBlocked.status == CompatibilityPublishStatus::Accepted);
		assert(ComponentFramesTo(otherFamilyBlocked.frames, component.connectionId).empty());

		*failA = false;
		const auto repaired = h.host.PublishCompatible(Publication(entity.playerId, SyntheticA, "syntheticA", 3, 44, { component.playerId }));
		assert(repaired.status == CompatibilityPublishStatus::Accepted);
		const auto repairedFrames = ComponentFramesTo(repaired.frames, component.connectionId);
		assert(repairedFrames.size() == 1);
		const auto& delta = repairedFrames.front().second;
		assert(delta.kind == FrameKind::Delta);
		bool sawA = false;
		bool sawB = false;
		for (const auto& changedEntity : delta.delta.changedEntities) {
			if (changedEntity.entityId != entity.playerId) {
				continue;
			}
			for (const auto& changed : changedEntity.changed) {
				sawA = sawA || (changed.family == SyntheticA && changed.data == ComponentBytes(44));
				sawB = sawB || (changed.family == SyntheticB && changed.data == ComponentBytes(55));
			}
		}
		assert(sawA && sawB);

		const auto finishB = h.host.PublishCompatible(Publication(entity.playerId, SyntheticB, "syntheticB", 1, 55, { component.playerId }));
		assert(finishB.status == CompatibilityPublishStatus::Accepted);
		assert(finishB.frames.empty());
	}

	void TestRouteReconciliationAndReplay()
	{
		Harness h;
		assert(h.host.RegisterCompatibilityAdapter(MakeAdapter(SyntheticA, "syntheticA", "component-synthetic-a-v1")));
		const auto component = h.Activate({ "component-snapshot-v1", "component-synthetic-a-v1" });
		const auto entity = h.Activate();
		const auto legacy = h.Activate();
		h.Position(component, 0.0);
		h.Position(entity, 100.0);
		h.Position(legacy, 200.0);

		const auto initial = h.host.PublishCompatible(Publication(entity.playerId, SyntheticA, "syntheticA", 7, 70, { component.playerId, legacy.playerId }));
		assert(ComponentFramesTo(initial.frames, component.connectionId).size() == 1);

		const std::vector<std::uint32_t> legacyOnly{ legacy.playerId };
		const auto removed = h.host.SetCompatibilityRecipients(entity.playerId, SyntheticA, legacyOnly);
		assert(removed.accepted);
		const auto removalFrames = ComponentFramesTo(removed.frames, component.connectionId);
		assert(removalFrames.size() == 1);
		assert(removalFrames.front().second.kind == FrameKind::Delta);
		bool removedEntity = std::find(
			removalFrames.front().second.delta.removedEntities.begin(),
			removalFrames.front().second.delta.removedEntities.end(),
			entity.playerId) != removalFrames.front().second.delta.removedEntities.end();
		bool removedFamily = false;
		for (const auto& changed : removalFrames.front().second.delta.changedEntities) {
			if (changed.entityId == entity.playerId) {
				removedFamily = std::find(changed.removedComponents.begin(), changed.removedComponents.end(), SyntheticA) != changed.removedComponents.end();
			}
		}
		assert(removedEntity || removedFamily);

		const std::vector<std::uint32_t> both{ component.playerId, legacy.playerId };
		const auto added = h.host.SetCompatibilityRecipients(entity.playerId, SyntheticA, both);
		assert(added.accepted);
		assert(ComponentFramesTo(added.frames, component.connectionId).size() == 1);
		const auto semanticReplay = h.host.PublishCompatible(Publication(entity.playerId, SyntheticA, "syntheticA", 7, 70, { component.playerId }));
		assert(semanticReplay.status == CompatibilityPublishStatus::Accepted);
		assert(semanticReplay.frames.empty());

		const std::vector<std::uint32_t> duplicateRoute{ component.playerId, component.playerId };
		assert(!h.host.SetCompatibilityRecipients(entity.playerId, SyntheticA, duplicateRoute).accepted);
	}

	void TestVisibilityExitReconcilesComponentState()
	{
		Harness h;
		assert(h.host.RegisterCompatibilityAdapter(MakeAdapter(SyntheticA, "syntheticA", "component-synthetic-a-v1")));
		const auto component = h.Activate({ "component-snapshot-v1", "component-synthetic-a-v1" });
		const auto entity = h.Activate();
		h.Position(component, 0.0);
		h.Position(entity, 100.0);
		const auto initial = h.host.PublishCompatible(Publication(entity.playerId, SyntheticA, "syntheticA", 1, 10, { component.playerId }));
		assert(ComponentFramesTo(initial.frames, component.connectionId).size() == 1);

		Json moved{
			{ "type", "transform" },
			{ "cellId", "00000999" },
			{ "worldspaceId", "0000DEAD" },
			{ "x", 0.0 },
			{ "y", 0.0 },
			{ "z", 0.0 },
			{ "angleZ", 0.0 },
			{ "movementType", "cell_change" }
		};
		const auto frames = h.host.HandleMessage(
			entity.connectionId,
			EncodeLegacySnapshot(moved, Family::Transform, ++h.transformSequence),
			h.Tick(),
			h.Tick());
		const auto componentFrames = ComponentFramesTo(frames, component.connectionId);
		assert(componentFrames.size() == 1);
		assert(componentFrames.front().second.kind == FrameKind::Delta);
	}

	void TestNoStaleValueOnNewerRouteAddFailure()
	{
		Harness h;
		auto fail = std::make_shared<bool>(false);
		assert(h.host.RegisterCompatibilityAdapter(MakeAdapter(SyntheticA, "syntheticA", "component-synthetic-a-v1", fail)));
		const auto firstPeer = h.Activate({ "component-snapshot-v1", "component-synthetic-a-v1" });
		const auto entity = h.Activate();
		const auto addedPeer = h.Activate({ "component-snapshot-v1", "component-synthetic-a-v1" });
		h.Position(firstPeer, 0.0);
		h.Position(entity, 100.0);
		h.Position(addedPeer, 200.0);
		const auto first = h.host.PublishCompatible(Publication(entity.playerId, SyntheticA, "syntheticA", 1, 10, { firstPeer.playerId }));
		assert(ComponentFramesTo(first.frames, firstPeer.connectionId).size() == 1);

		*fail = true;
		const auto newer = h.host.PublishCompatible(Publication(entity.playerId, SyntheticA, "syntheticA", 2, 20, { firstPeer.playerId, addedPeer.playerId }));
		assert(newer.status == CompatibilityPublishStatus::Accepted);
		assert(ComponentFramesTo(newer.frames, addedPeer.connectionId).empty());
		*fail = false;
		const auto retry = h.host.PublishCompatible(Publication(entity.playerId, SyntheticA, "syntheticA", 2, 20, {}));
		assert(retry.status == CompatibilityPublishStatus::Accepted);
		const auto addedFrames = ComponentFramesTo(retry.frames, addedPeer.connectionId);
		assert(addedFrames.size() == 1);
		const auto decoded = addedFrames.front().second;
		assert(decoded.kind == FrameKind::Baseline);
		assert(decoded.baseline.at(ComponentKey{ entity.playerId, SyntheticA }) == ComponentBytes(20));
	}

	void TestRawComponentIngressStillRejected()
	{
		ProtocolHost host{ "Compatibility Shim Test" };
		const auto welcome = host.HandleConnected(10, 1000.0);
		assert(welcome.size() == 1);
		const auto ready = host.HandleMessage(10, EncodeJson(Json{ { "type", "hello" }, { "protocolVersion", 2 } }), 1.0, 1000.1);
		assert(!ready.empty());
		SnapshotState state;
		state[ComponentKey{ 1, Family::Transform }] = { 1, 2, 3, 4 };
		const auto componentFrame = EncodeBaseline(state, 1);
		assert(componentFrame.has_value());
		const auto before = host.Stats().packetsRejected;
		assert(host.HandleMessage(10, *componentFrame, 2.0, 1000.2).empty());
		assert(host.Stats().packetsRejected == before + 1);
	}
}

int main()
{
	TestCapabilityValidationAndRegistration();
	TestDirectPublicationModeGuard();
	TestOneWayLegacyToComponentAdapter();
	TestReliableChannelNegotiation();
	TestMixedFanoutRevisionOrderingAndDirtyRecovery();
	TestRouteReconciliationAndReplay();
	TestVisibilityExitReconcilesComponentState();
	TestNoStaleValueOnNewerRouteAddFailure();
	TestRawComponentIngressStillRejected();
}
