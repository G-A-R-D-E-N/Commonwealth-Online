%:include "F4TCompatibilityShim.h"

%:include <algorithm>
%:include <cassert>
%:include <charconv>
%:include <cstddef>
%:include <cstdint>
%:include <memory>
%:include <span>
%:include <string>
%:include <utility>
%:include <vector>

namespace
{
	using namespace F4T::Compatibility;
	using namespace F4T::Replication;

	constexpr Family SyntheticFamily = static_cast<Family>(240);
	constexpr Family OutboundOnlyFamily = static_cast<Family>(238);

	struct SyntheticValue
	{
		std::uint32_t value{};

		[[nodiscard]] bool operator==(const SyntheticValue&) const = default;
	};

	struct SyntheticComponent : ISnapshotComponent
	{
		std::uint32_t value{};

		SyntheticComponent() = default;
		explicit SyntheticComponent(std::uint32_t a_value) : value(a_value) {}

		[[nodiscard]] Family ComponentFamily() const noexcept override { return SyntheticFamily; }
		[[nodiscard]] AuthorityClass Authority() const noexcept override { return AuthorityClass::Server; }
		[[nodiscard]] Channel PreferredChannel() const noexcept override { return Channel::Unreliable; }
		void Serialize(SnapshotStream& a_stream) noexcept override { a_stream.U32(value); }
	};

	std::vector<std::uint8_t> SyntheticBytes(std::uint32_t a_value)
	{
		SyntheticComponent component{ a_value };
		auto stream = SnapshotStream::Writer();
		component.Serialize(stream);
		return { stream.Written().begin(), stream.Written().end() };
	}

	FamilyAdapter MakeSyntheticAdapter()
	{
		FamilyAdapter adapter;
		adapter.metadata.legacy.name = "syntheticState";
		adapter.metadata.component.family = SyntheticFamily;
		adapter.metadata.supportedDirections = AllConversionDirections;
		adapter.metadata.requiredComponentCapability = "component-synthetic-v1";
		adapter.legacyToCanonical = [](std::string_view a_payload) -> std::optional<CanonicalValue> {
			constexpr std::string_view prefix = "synthetic:";
			if (!a_payload.starts_with(prefix)) {
				return std::nullopt;
			}
			std::uint32_t value = 0;
			const auto digits = a_payload.substr(prefix.size());
			const auto [end, error] = std::from_chars(digits.data(), digits.data() + digits.size(), value);
			if (error != std::errc{} || end != digits.data() + digits.size()) {
				return std::nullopt;
			}
			return CanonicalValue{ SyntheticValue{ value } };
		};
		adapter.canonicalToLegacy = [](const CanonicalValue& a_value) -> std::optional<std::string> {
			const auto* value = std::any_cast<SyntheticValue>(&a_value);
			if (value == nullptr) {
				return std::nullopt;
			}
			return std::string{ "synthetic:" } + std::to_string(value->value);
		};
		adapter.canonicalToComponent = [](const CanonicalValue& a_value) -> std::optional<std::vector<std::uint8_t>> {
			const auto* value = std::any_cast<SyntheticValue>(&a_value);
			return value == nullptr ? std::nullopt : std::optional<std::vector<std::uint8_t>>{ SyntheticBytes(value->value) };
		};
		adapter.componentToCanonical = [](std::span<const std::uint8_t> a_payload) -> std::optional<CanonicalValue> {
			auto stream = SnapshotStream::Reader(a_payload);
			SyntheticComponent component;
			component.Serialize(stream);
			if (!stream.Ok() || stream.BytesRead() != a_payload.size()) {
				return std::nullopt;
			}
			return CanonicalValue{ SyntheticValue{ component.value } };
		};
		return adapter;
	}

	FamilyAdapter MakeOutboundOnlyAdapter()
	{
		auto adapter = MakeSyntheticAdapter();
		adapter.metadata.legacy.name = "outboundOnlyState";
		adapter.metadata.component.family = OutboundOnlyFamily;
		adapter.metadata.supportedDirections = LegacyToComponentDirection;
		adapter.metadata.requiredComponentCapability = "component-outbound-only-v1";
		adapter.canonicalToLegacy = {};
		adapter.componentToCanonical = {};
		return adapter;
	}

	std::vector<std::uint8_t> Bytes(std::string_view a_text)
	{
		return { a_text.begin(), a_text.end() };
	}

	std::vector<std::uint8_t> Cog2(std::string_view a_json, Family a_family, std::uint32_t a_sequence)
	{
		(void)a_family;
		(void)a_sequence;
		std::vector<std::uint8_t> payload(F4T::GnsSnapshotEnvelope::HeaderSize + a_json.size());
		assert(F4T::GnsSnapshotEnvelope::WriteHeader(payload, a_family, a_sequence));
		std::copy(a_json.begin(), a_json.end(), payload.begin() + static_cast<std::ptrdiff_t>(F4T::GnsSnapshotEnvelope::HeaderSize));
		return payload;
	}

	SessionCapabilities ComponentCapabilities()
	{
		return SessionCapabilities{ { "component-snapshot-v1", "component-synthetic-v1" } };
	}

	void TestRegistryAndSyntheticComponent()
	{
		CompatibilityRegistry registry;
		assert(registry.Register(MakeSyntheticAdapter()));
		assert(registry.FindByLegacyFamily("syntheticState") != nullptr);
		assert(registry.FindByComponentFamily(SyntheticFamily) != nullptr);
		assert(registry.SupportsComponent(SyntheticFamily));
		assert(registry.FindByComponentFamily(Family::Transform) == nullptr);
		assert(!registry.SupportsComponent(Family::Transform));
		assert(registry.RequiredComponentCapability(SyntheticFamily) == "component-synthetic-v1");
		assert(!registry.Register(MakeSyntheticAdapter()));

		FamilyAdapter duplicateComponent = MakeSyntheticAdapter();
		duplicateComponent.metadata.legacy.name = "otherSyntheticState";
		assert(!registry.Register(std::move(duplicateComponent)));

		FamilyAdapter duplicateLegacy = MakeSyntheticAdapter();
		duplicateLegacy.metadata.component.family = static_cast<Family>(239);
		assert(!registry.Register(std::move(duplicateLegacy)));

		FamilyAdapter invalid = MakeSyntheticAdapter();
		invalid.metadata.legacy.name = "";
		assert(!registry.Register(std::move(invalid)));

		FamilyAdapter unknownDirection = MakeSyntheticAdapter();
		unknownDirection.metadata.legacy.name = "unknownDirection";
		unknownDirection.metadata.component.family = static_cast<Family>(237);
		unknownDirection.metadata.supportedDirections = 4;
		assert(!registry.Register(std::move(unknownDirection)));

		FamilyAdapter noHooks = MakeSyntheticAdapter();
		noHooks.metadata.legacy.name = "noHooks";
		noHooks.metadata.component.family = static_cast<Family>(236);
		noHooks.legacyToCanonical = {};
		noHooks.canonicalToLegacy = {};
		noHooks.canonicalToComponent = {};
		noHooks.componentToCanonical = {};
		assert(!registry.Register(std::move(noHooks)));

		FamilyAdapter incompleteDeclaredDirection = MakeSyntheticAdapter();
		incompleteDeclaredDirection.metadata.legacy.name = "incompleteDeclaredDirection";
		incompleteDeclaredDirection.metadata.component.family = static_cast<Family>(235);
		incompleteDeclaredDirection.componentToCanonical = {};
		assert(!registry.Register(std::move(incompleteDeclaredDirection)));

		CompatibilityRegistry oneWayRegistry;
		assert(oneWayRegistry.Register(MakeOutboundOnlyAdapter()));
		assert(oneWayRegistry.FindByLegacyFamily("outboundOnlyState") != nullptr);
		assert(!oneWayRegistry.SupportsComponent(OutboundOnlyFamily));

		ComponentRegistry components;
		assert(components.Register<SyntheticComponent>());
		assert(components.Contains(SyntheticFamily));
		std::vector<EntityComponentSet> outgoing;
		EntityComponentSet entity;
		entity.entityId = 44;
		entity.components.push_back(std::make_unique<SyntheticComponent>(42));
		outgoing.push_back(std::move(entity));
		auto writer = SnapshotStream::Writer();
		assert(WriteEntityComponents(writer, outgoing));
		auto reader = SnapshotStream::Reader(writer.Written());
		const auto decoded = ReadEntityComponents(reader, components);
		assert(decoded.has_value());
		assert(decoded->size() == 1);
		assert(decoded->front().components.size() == 1);
		assert(static_cast<const SyntheticComponent*>(decoded->front().components.front().get())->value == 42);
	}

	void TestProjectionPolicyAndLedger()
	{
		CompatibilityRegistry registry;
		assert(registry.Register(MakeSyntheticAdapter()));
		const RepresentationPolicy policy{ registry };
		const RepresentationSelectionKey legacyKey{ 10, 20, SyntheticFamily, 1 };
		const RepresentationSelectionKey componentKey{ 11, 20, SyntheticFamily, 1 };
		const auto legacy = policy.Select(legacyKey, "syntheticState", SessionCapabilities{});
		const auto component = policy.Select(componentKey, "syntheticState", ComponentCapabilities());
		assert(legacy.has_value());
		assert(component.has_value());
		assert(legacy->representation == Representation::Legacy);
		assert(component->representation == Representation::Component);
		const auto globalOnly = policy.Select(componentKey, "syntheticState", SessionCapabilities{ { "component-snapshot-v1" } });
		const auto familyOnly = policy.Select(componentKey, "syntheticState", SessionCapabilities{ { "component-synthetic-v1" } });
		assert(globalOnly.has_value() && globalOnly->representation == Representation::Legacy);
		assert(familyOnly.has_value() && familyOnly->representation == Representation::Legacy);
		assert(!policy.Select(RepresentationSelectionKey{ 10, 20, Family::Transform, 1 }, "syntheticState", ComponentCapabilities()).has_value());
		assert(policy.Select(legacyKey, "unknown", ComponentCapabilities()) == std::nullopt);

		const CanonicalValue value{ SyntheticValue{ 42 } };
		const auto decodedLegacy = DecodeLegacy(*legacy->adapter, "synthetic:42");
		const auto encodedLegacy = EncodeLegacy(*legacy->adapter, value);
		const auto encodedComponent = EncodeComponent(*component->adapter, value);
		const auto decodedComponent = DecodeComponent(*component->adapter, SyntheticBytes(42));
		assert(decodedLegacy.has_value());
		assert(std::any_cast<SyntheticValue>(*decodedLegacy) == SyntheticValue{ 42 });
		assert(encodedLegacy.has_value() && *encodedLegacy == "synthetic:42");
		assert(encodedComponent.has_value() && *encodedComponent == SyntheticBytes(42));
		assert(decodedComponent.has_value());
		assert(std::any_cast<SyntheticValue>(*decodedComponent) == SyntheticValue{ 42 });
		const auto legacyProjection = Project(*legacy, value);
		const auto componentProjection = Project(*component, value);
		assert(legacyProjection.has_value());
		assert(componentProjection.has_value());
		assert(std::get<std::string>(legacyProjection->payload) == "synthetic:42");
		assert(std::get<std::vector<std::uint8_t>>(componentProjection->payload) == SyntheticBytes(42));

		RepresentationLedger ledger;
		assert(ledger.Commit(*legacy));
		assert(ledger.Contains(legacyKey));
		assert(!ledger.Commit(*legacy));
		assert(ledger.Commit(*component));
		auto mismatchedSelection = *component;
		mismatchedSelection.key.componentFamily = Family::Transform;
		assert(!ledger.Commit(mismatchedSelection));
		assert(!Project(mismatchedSelection, value).has_value());
		ledger.Clear();
		assert(!ledger.Contains(legacyKey));

		CompatibilityRegistry oneWayRegistry;
		assert(oneWayRegistry.Register(MakeOutboundOnlyAdapter()));
		const RepresentationPolicy oneWayPolicy{ oneWayRegistry };
		const RepresentationSelectionKey oneWayKey{ 12, 20, OutboundOnlyFamily, 1 };
		const SessionCapabilities oneWayCapabilities{ { "component-snapshot-v1", "component-outbound-only-v1" } };
		const auto oneWayComponent = oneWayPolicy.Select(oneWayKey, "outboundOnlyState", oneWayCapabilities);
		assert(oneWayComponent.has_value());
		assert(oneWayComponent->representation == Representation::Component);
		assert(Project(*oneWayComponent, value).has_value());
		assert(!oneWayPolicy.Select(oneWayKey, "outboundOnlyState", SessionCapabilities{}).has_value());
		RepresentationLedger oneWayLedger;
		assert(oneWayLedger.Commit(*oneWayComponent));
	}

	void TestSnapshotRoundTrip()
	{
		CompatibilityRegistry registry;
		assert(registry.Register(MakeSyntheticAdapter()));
		const auto registered = [&registry](Family a_family) {
			return registry.SupportsComponent(a_family);
		};
		SnapshotState baselineState;
		baselineState[ComponentKey{ 20, SyntheticFamily }] = SyntheticBytes(42);
		const auto baseline = EncodeBaseline(baselineState, 1);
		assert(baseline.has_value());
		DecodedFrame baselineFrame;
		assert(DecodeFrame(*baseline, baselineFrame, registered) == DecodeStatus::Valid);
		SnapshotReceiver receiver;
		assert(receiver.AcceptBaseline(baselineFrame.baseline, baselineFrame.snapshotSequence));

		SnapshotState changedState;
		changedState[ComponentKey{ 20, SyntheticFamily }] = SyntheticBytes(43);
		const auto delta = EncodeDelta(ComputeDelta(baselineState, changedState), 2, 1);
		assert(delta.has_value());
		DecodedFrame deltaFrame;
		assert(DecodeFrame(*delta, deltaFrame, registered) == DecodeStatus::Valid);
		assert(receiver.AcceptDelta(deltaFrame.delta, deltaFrame.snapshotSequence, deltaFrame.baselineSequence));
		assert(receiver.CurrentView() == changedState);

		const auto emptyDelta = EncodeDelta(ComputeDelta(baselineState, baselineState), 3, 1);
		assert(emptyDelta.has_value());
		DecodedFrame emptyDeltaFrame;
		assert(DecodeFrame(*emptyDelta, emptyDeltaFrame, registered) == DecodeStatus::Valid);
		assert(emptyDeltaFrame.delta.Empty());
		assert(receiver.AcceptDelta(emptyDeltaFrame.delta, emptyDeltaFrame.snapshotSequence, emptyDeltaFrame.baselineSequence));
		assert(receiver.CurrentView() == baselineState);
		assert(!receiver.AcceptDelta(emptyDeltaFrame.delta, 4, 99));
		assert(receiver.CurrentView() == baselineState);
	}

	void TestReceiveDemux()
	{
		CompatibilityRegistry registry;
		assert(registry.Register(MakeSyntheticAdapter()));
		ComponentRegistry definitions;
		assert(definitions.RegisterDefinition(SyntheticFamily, AuthorityClass::Server, Channel::Unreliable));
		const auto frozen = definitions.Freeze();
		assert(frozen.has_value());
		const ReceiveDemux demux{ registry, *frozen };
		SnapshotState state;
		state[ComponentKey{ 20, SyntheticFamily }] = SyntheticBytes(42);
		const auto baseline = EncodeBaseline(state, 1);
		assert(baseline.has_value());
		const auto accepted = demux.Classify(*baseline, ComponentCapabilities());
		assert(accepted.classification == ReceiveClassification::Component);
		assert(accepted.componentFrame.has_value());
		assert(accepted.componentFrame->kind == FrameKind::Baseline);
		assert(accepted.componentFrame->baselineSequence == 0);
		assert(accepted.componentFrame->snapshotSequence == 1);

		const auto notNegotiated = demux.Classify(*baseline, SessionCapabilities{});
		assert(notNegotiated.classification == ReceiveClassification::Rejected);
		assert(notNegotiated.rejection == ReceiveRejectReason::ComponentNotNegotiated);

		SnapshotState unknownState;
		unknownState[ComponentKey{ 20, static_cast<Family>(241) }] = { 1, 2, 3, 4 };
		const auto unknown = EncodeBaseline(unknownState, 8);
		assert(unknown.has_value());
		const auto unknownResult = demux.Classify(*unknown, ComponentCapabilities());
		assert(unknownResult.classification == ReceiveClassification::Rejected);
		assert(unknownResult.rejection == ReceiveRejectReason::InvalidComponent);

		SnapshotState unregisteredProductionState;
		unregisteredProductionState[ComponentKey{ 20, Family::Transform }] = { 1, 2, 3, 4 };
		const auto unregisteredProduction = EncodeBaseline(unregisteredProductionState, 9);
		assert(unregisteredProduction.has_value());
		const auto unregisteredProductionResult = demux.Classify(*unregisteredProduction, ComponentCapabilities());
		assert(unregisteredProductionResult.classification == ReceiveClassification::Rejected);
		assert(unregisteredProductionResult.rejection == ReceiveRejectReason::InvalidComponent);

		SnapshotState invalidState;
		invalidState[ComponentKey{ 20, SyntheticFamily }] = { 1 };
		const auto invalid = EncodeBaseline(invalidState, 10);
		assert(invalid.has_value());
		const auto invalidResult = demux.Classify(*invalid, ComponentCapabilities());
		assert(invalidResult.classification == ReceiveClassification::Rejected);
		assert(invalidResult.rejection == ReceiveRejectReason::InvalidComponent);

		const std::vector<std::uint8_t> malformedBinary{ 1, 0, 0, 0 };
		const auto malformedBinaryResult = demux.Classify(malformedBinary, ComponentCapabilities());
		assert(malformedBinaryResult.classification == ReceiveClassification::Rejected);
		assert(malformedBinaryResult.rejection == ReceiveRejectReason::InvalidComponent);
	}

	void TestLegacyClassificationOrder()
	{
		CompatibilityRegistry registry;
		assert(registry.Register(MakeSyntheticAdapter()));
		const ReceiveDemux demux{ registry };
		const auto cog2 = Cog2(R"({"type":"transform","x":1})", Family::Transform, 1);
		const auto legacy = demux.Classify(cog2, ComponentCapabilities());
		assert(legacy.classification == ReceiveClassification::Legacy);
		assert(legacy.legacyPayloadOffset == F4T::GnsSnapshotEnvelope::HeaderSize);
		assert(legacy.legacyHeader.has_value());
		assert(legacy.legacyHeader->family == Family::Transform);

		const auto malformedCog2 = demux.Classify(std::vector<std::uint8_t>{ 'C', 'O', 'G', '2' }, ComponentCapabilities());
		assert(malformedCog2.classification == ReceiveClassification::Rejected);
		assert(malformedCog2.rejection == ReceiveRejectReason::MalformedCog2);

		const auto mismatch = Cog2(R"({"type":"npcState"})", Family::Transform, 2);
		const auto mismatchResult = demux.Classify(mismatch, ComponentCapabilities());
		assert(mismatchResult.classification == ReceiveClassification::Rejected);
		assert(mismatchResult.rejection == ReceiveRejectReason::MalformedCog2);

		const auto json = demux.Classify(Bytes("  {\"type\":\"hello\"}"), ComponentCapabilities());
		assert(json.classification == ReceiveClassification::Legacy);
		assert(json.legacyPayloadOffset == 2);

		const auto malformedJson = demux.Classify(Bytes("{\"type\":"), ComponentCapabilities());
		assert(malformedJson.classification == ReceiveClassification::Rejected);
		assert(malformedJson.rejection == ReceiveRejectReason::MalformedJson);

		const auto empty = demux.Classify(Bytes(" \t\r\n"), ComponentCapabilities());
		assert(empty.classification == ReceiveClassification::Rejected);
		assert(empty.rejection == ReceiveRejectReason::EmptyPayload);
	}

	void TestReliableDemuxGating()
	{
		CompatibilityRegistry registry;
		assert(registry.Register(MakeSyntheticAdapter()));
		ComponentRegistry definitions;
		assert(definitions.RegisterDefinition(SyntheticFamily, AuthorityClass::Server, Channel::Reliable));
		const auto frozen = definitions.Freeze();
		assert(frozen.has_value());
		const ReceiveDemux demux{ registry, *frozen };
		SnapshotState state;
		state[ComponentKey{ 20, SyntheticFamily }] = SyntheticBytes(7);
		const auto reliable = EncodeBaseline(state, 1, Channel::Reliable);
		assert(reliable.has_value());
		const auto missingChannel = demux.Classify(*reliable, ComponentCapabilities());
		assert(missingChannel.classification == ReceiveClassification::Rejected);
		const auto accepted = demux.Classify(*reliable, SessionCapabilities{ { "component-snapshot-v1", "component-synthetic-v1", "component-snapshot-channels-v1" } });
		assert(accepted.classification == ReceiveClassification::Component);
		assert(accepted.componentFrame->channel == Channel::Reliable);

		const auto emptyReliable = EncodeBaseline({}, 2, Channel::Reliable);
		assert(emptyReliable.has_value());
		const auto emptyMissingChannel = demux.Classify(*emptyReliable, ComponentCapabilities());
		assert(emptyMissingChannel.classification == ReceiveClassification::Rejected);
		const auto emptyAccepted = demux.Classify(*emptyReliable, SessionCapabilities{ { "component-snapshot-v1", "component-snapshot-channels-v1" } });
		assert(emptyAccepted.classification == ReceiveClassification::Component);

		SnapshotDelta removalOnly;
		removalOnly.changedEntities.push_back(ChangedEntity{ 20, {}, { SyntheticFamily } });
		const auto reliableRemoval = EncodeDelta(removalOnly, 3, 1, Channel::Reliable);
		assert(reliableRemoval.has_value());
		const auto removalMissingChannel = demux.Classify(*reliableRemoval, SessionCapabilities{ { "component-snapshot-v1", "component-synthetic-v1" } });
		assert(removalMissingChannel.classification == ReceiveClassification::Rejected);
		const auto removalAccepted = demux.Classify(*reliableRemoval, SessionCapabilities{ { "component-snapshot-v1", "component-synthetic-v1", "component-snapshot-channels-v1" } });
		assert(removalAccepted.classification == ReceiveClassification::Component);

		const ReceiveDemux missingDefinitions{ registry };
		const auto missingDefinitionsResult = missingDefinitions.Classify(*reliable, SessionCapabilities{ { "component-snapshot-v1", "component-synthetic-v1", "component-snapshot-channels-v1" } });
		assert(missingDefinitionsResult.classification == ReceiveClassification::Rejected);
	}
}

int main()
{
	TestRegistryAndSyntheticComponent();
	TestProjectionPolicyAndLedger();
	TestSnapshotRoundTrip();
	TestReceiveDemux();
	TestLegacyClassificationOrder();
	TestReliableDemuxGating();
}
