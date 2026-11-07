#include "F4TValidatorFuzzHarness.h"

#include "F4TCompatibilityShim.h"
#include "F4TSnapshotDelta.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <iterator>
#include <map>
#include <optional>
#include <set>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace
{
	using namespace ValidatorFuzz;
	using namespace F4T::Replication;
	using Bytes = std::vector<std::uint8_t>;
	using F4T::Compatibility::AllConversionDirections;
	using F4T::Compatibility::CanonicalValue;
	using F4T::Compatibility::ComponentToLegacyDirection;
	using F4T::Compatibility::ConversionDirections;
	using F4T::Compatibility::DecodeComponent;
	using F4T::Compatibility::DecodeLegacy;
	using F4T::Compatibility::FamilyAdapter;
	using F4T::Compatibility::LegacyToComponentDirection;

	constexpr std::string_view kDecodeFrame = "SnapshotDeltaWire::DecodeFrame";
	constexpr std::string_view kDecodeComponent = "Compatibility::DecodeComponent";
	constexpr std::string_view kDecodeLegacy = "Compatibility::DecodeLegacy";
	constexpr std::size_t kCorpusSize = 8;
	constexpr std::size_t kMaxCorpusEntities = 12;

	bool AllRegistered(Family) { return true; }
	bool TransformOrNpc(Family a_family) { return a_family == Family::Transform || a_family == Family::NpcState; }

	template <typename T>
	T FreshValue(Rng& a_rng, std::set<T>& a_used)
	{
		T value{};
		do {
			value = static_cast<T>(a_rng.engine());
		} while (!a_used.insert(value).second);
		return value;
	}

	Channel RandomChannel(Rng& a_rng)
	{
		return a_rng.Below(2) == 0 ? Channel::Reliable : Channel::Unreliable;
	}

	Bytes RandomData(Rng& a_rng)
	{
		Bytes data(a_rng.Below(48));
		for (auto& byte : data) {
			byte = RandomByte(a_rng);
		}
		return data;
	}

	Bytes Encoded(const Trial& a_trial, std::optional<Bytes> a_bytes)
	{
		if (!a_bytes) {
			Fail(a_trial, "seed corpus frame could not be encoded");
		}
		return std::move(*a_bytes);
	}

	Bytes BaselineFrame(const Trial& a_trial, Rng& a_rng)
	{
		SnapshotState state;
		for (std::size_t e = 1 + a_rng.Below(kMaxCorpusEntities); e > 0; --e) {
			const auto entityId = static_cast<std::uint32_t>(a_rng.engine());
			for (std::size_t c = 1 + a_rng.Below(4); c > 0; --c) {
				state[ComponentKey{ entityId, static_cast<Family>(a_rng.Below(256)) }] = RandomData(a_rng);
			}
		}
		const auto snapSeq = static_cast<std::uint32_t>(1 + a_rng.Below(100000));
		return Encoded(a_trial, EncodeBaseline(state, snapSeq, RandomChannel(a_rng)));
	}

	Bytes DeltaFrame(const Trial& a_trial, Rng& a_rng)
	{
		SnapshotDelta delta;
		std::set<std::uint32_t> entityIds;
		for (std::size_t e = 1 + a_rng.Below(6); e > 0; --e) {
			ChangedEntity entity;
			entity.entityId = FreshValue(a_rng, entityIds);
			std::set<std::uint8_t> families;
			for (std::size_t c = 1 + a_rng.Below(3); c > 0; --c) {
				entity.changed.push_back(ChangedComponent{ static_cast<Family>(FreshValue<std::uint8_t>(a_rng, families)), RandomData(a_rng) });
			}
			for (std::size_t c = a_rng.Below(3); c > 0; --c) {
				entity.removedComponents.push_back(static_cast<Family>(FreshValue<std::uint8_t>(a_rng, families)));
			}
			delta.changedEntities.push_back(std::move(entity));
		}
		for (std::size_t r = a_rng.Below(4); r > 0; --r) {
			delta.removedEntities.push_back(FreshValue(a_rng, entityIds));
		}
		const auto snapSeq = static_cast<std::uint32_t>(1 + a_rng.Below(100000));
		const auto baseSeq = static_cast<std::uint32_t>(1 + a_rng.Below(100000));
		return Encoded(a_trial, EncodeDelta(delta, snapSeq, baseSeq, RandomChannel(a_rng)));
	}

	std::vector<Bytes> BuildCorpus(const Trial& a_trial, Rng& a_rng)
	{
		std::vector<Bytes> corpus;
		for (std::size_t index = 0; index < kCorpusSize; ++index) {
			corpus.push_back(index % 2 == 0 ? BaselineFrame(a_trial, a_rng) : DeltaFrame(a_trial, a_rng));
		}
		return corpus;
	}

	DecodeStatus StatusOf(const Trial& a_trial, std::span<const std::uint8_t> a_bytes, const IsRegisteredFn& a_isRegistered)
	{
		DecodedFrame frame;
		return Invoke(a_trial, [&] { return DecodeFrame(a_bytes, frame, a_isRegistered); });
	}

	void Expect(const Trial& a_trial, const Bytes& a_bytes, DecodeStatus a_expected, const IsRegisteredFn& a_isRegistered)
	{
		const auto status = StatusOf(a_trial, a_bytes, a_isRegistered);
		if (status != a_expected) {
			Fail(a_trial, "status " + std::to_string(static_cast<int>(status)) + " where " + std::to_string(static_cast<int>(a_expected)) + " was expected; size=" + std::to_string(a_bytes.size()));
		}
	}

	std::optional<Bytes> EncodeFrame(const DecodedFrame& a_frame)
	{
		if (a_frame.kind == FrameKind::Baseline) {
			return EncodeBaseline(a_frame.baseline, a_frame.snapshotSequence, a_frame.channel);
		}
		return EncodeDelta(a_frame.delta, a_frame.snapshotSequence, a_frame.baselineSequence, a_frame.channel);
	}

	void CheckAccepted(const Trial& a_trial, const Bytes& a_input, const DecodedFrame& a_frame, const IsRegisteredFn& a_isRegistered)
	{
		const bool baseline = a_frame.kind == FrameKind::Baseline;
		if (a_frame.snapshotSequence == 0 || (baseline && a_frame.baselineSequence != 0) || (!baseline && a_frame.baselineSequence == 0)) {
			Fail(a_trial, "accepted frame breaks the sequence rules");
		}
		if (baseline) {
			std::map<std::uint32_t, std::size_t> perEntity;
			for (const auto& [key, data] : a_frame.baseline) {
				++perEntity[key.entityId];
				if (!a_isRegistered(key.family) || data.size() > SnapMaxComponentBytes) {
					Fail(a_trial, "accepted baseline component is unregistered or oversized");
				}
			}
			if (perEntity.size() > SnapMaxEntities ||
			    std::any_of(perEntity.begin(), perEntity.end(), [](const auto& a_entry) { return a_entry.second > SnapMaxComponentsPerEntity; })) {
				Fail(a_trial, "accepted baseline exceeds entity or per-entity component cap");
			}
		} else {
			const auto& delta = a_frame.delta;
			if (delta.changedEntities.size() > SnapMaxEntities || delta.removedEntities.size() > SnapMaxEntities) {
				Fail(a_trial, "accepted delta exceeds entity cap");
			}
			std::set<std::uint32_t> changedIds;
			for (const auto& entity : delta.changedEntities) {
				if (!changedIds.insert(entity.entityId).second || (entity.changed.empty() && entity.removedComponents.empty()) ||
				    entity.changed.size() > SnapMaxComponentsPerEntity || entity.removedComponents.size() > SnapMaxComponentsPerEntity) {
					Fail(a_trial, "accepted delta entity is duplicated, empty or over the component cap");
				}
				std::set<std::uint8_t> families;
				for (const auto& component : entity.changed) {
					if (!a_isRegistered(component.family) || component.data.size() > SnapMaxComponentBytes ||
					    !families.insert(static_cast<std::uint8_t>(component.family)).second) {
						Fail(a_trial, "accepted delta changed component is unregistered, oversized or duplicated");
					}
				}
				for (const Family family : entity.removedComponents) {
					if (!a_isRegistered(family) || !families.insert(static_cast<std::uint8_t>(family)).second) {
						Fail(a_trial, "accepted delta removed component is unregistered, duplicated or conflicting");
					}
				}
			}
			std::set<std::uint32_t> removedIds;
			for (const std::uint32_t entityId : delta.removedEntities) {
				if (!removedIds.insert(entityId).second || changedIds.count(entityId) != 0) {
					Fail(a_trial, "accepted delta removed entity is duplicated or conflicting");
				}
			}
		}

		const auto encoded = EncodeFrame(a_frame);
		if (!encoded || encoded->size() != a_input.size()) {
			Fail(a_trial, "accepted frame does not re-encode to the input length");
		}
		DecodedFrame again;
		if (Invoke(a_trial, [&] { return DecodeFrame(*encoded, again, a_isRegistered); }) != DecodeStatus::Valid) {
			Fail(a_trial, "re-encoded accepted frame is rejected");
		}
		const auto reencoded = EncodeFrame(again);
		if (!reencoded || *reencoded != *encoded) {
			Fail(a_trial, "re-decoded frame does not re-encode to the same bytes");
		}
	}

	void CheckKnownBad(const Trial& a_trial, const Bytes& a_valid, Rng& a_rng)
	{
		for (std::size_t length = 0; length < a_valid.size(); ++length) {
			const std::span<const std::uint8_t> prefix(a_valid.data(), length);
			if (length < SnapFrameHeaderSize) {
				if (StatusOf(a_trial, prefix, AllRegistered) != DecodeStatus::HeaderTruncated) {
					Fail(a_trial, "short header was not reported as HeaderTruncated");
				}
			} else if (StatusOf(a_trial, prefix, AllRegistered) == DecodeStatus::Valid) {
				Fail(a_trial, "strict prefix of a valid frame was accepted; length=" + std::to_string(length));
			}
		}

		Bytes extended = a_valid;
		for (std::size_t count = 1 + a_rng.Below(8); count > 0; --count) {
			extended.push_back(RandomByte(a_rng));
		}
		Expect(a_trial, extended, DecodeStatus::TrailingBytes, AllRegistered);

		const bool baseline = a_valid[1] == static_cast<std::uint8_t>(FrameKind::Baseline);
		Bytes mutated = a_valid;
		mutated[0] = static_cast<std::uint8_t>(a_valid[0] ^ 0x80U);
		Expect(a_trial, mutated, DecodeStatus::BadVersion, AllRegistered);

		mutated = a_valid;
		mutated[1] = static_cast<std::uint8_t>(2 + a_rng.Below(254));
		Expect(a_trial, mutated, DecodeStatus::BadKind, AllRegistered);

		mutated = a_valid;
		mutated[2] = static_cast<std::uint8_t>(a_valid[2] | 0x02U);
		Expect(a_trial, mutated, DecodeStatus::ReservedNotZero, AllRegistered);

		mutated = a_valid;
		mutated[3] = 1;
		Expect(a_trial, mutated, DecodeStatus::ReservedNotZero, AllRegistered);

		mutated = a_valid;
		std::fill(mutated.begin() + 4, mutated.begin() + 8, std::uint8_t{ 0 });
		Expect(a_trial, mutated, DecodeStatus::ZeroSnapshotSequence, AllRegistered);

		mutated = a_valid;
		if (baseline) {
			mutated[11] = 1;
			Expect(a_trial, mutated, DecodeStatus::BaselineHasBaseSequence, AllRegistered);
		} else {
			std::fill(mutated.begin() + 8, mutated.begin() + 12, std::uint8_t{ 0 });
			Expect(a_trial, mutated, DecodeStatus::ZeroBaselineSequence, AllRegistered);
		}

		mutated = a_valid;
		mutated[SnapFrameHeaderSize] = 0x10;
		mutated[SnapFrameHeaderSize + 1] = 0x01;
		Expect(a_trial, mutated, DecodeStatus::CountExceedsMax, AllRegistered);

		DecodedFrame frame;
		if (Invoke(a_trial, [&] { return DecodeFrame(a_valid, frame, AllRegistered); }) != DecodeStatus::Valid) {
			Fail(a_trial, "known-bad base frame was not valid");
		}
		const Family target = baseline ? frame.baseline.begin()->first.family
		                               : frame.delta.changedEntities.front().changed.front().family;
		Expect(a_trial, a_valid, DecodeStatus::UnknownFamily, [target](Family a_family) { return a_family != target; });
		Expect(a_trial, a_valid, DecodeStatus::NoRegistry, IsRegisteredFn{});
	}

	void CorruptLengthField(Bytes& a_bytes, Rng& a_rng)
	{
		if (a_bytes.size() < SnapFrameHeaderSize + 2) {
			return;
		}
		const std::size_t offset = SnapFrameHeaderSize + a_rng.Below(a_bytes.size() - SnapFrameHeaderSize - 1);
		const auto value = static_cast<std::uint16_t>(a_rng.engine());
		a_bytes[offset] = static_cast<std::uint8_t>(value >> 8U);
		a_bytes[offset + 1] = static_cast<std::uint8_t>(value & 0xFFU);
	}

	void FuzzDecodeFrame(std::uint64_t a_seed)
	{
		Rng rng{ std::mt19937_64{ a_seed } };
		const Trial corpusTrial{ kDecodeFrame, a_seed, 0 };
		const auto corpus = BuildCorpus(corpusTrial, rng);
		for (std::size_t index = 0; index < corpus.size(); ++index) {
			const Trial trial{ kDecodeFrame, a_seed, index };
			DecodedFrame frame;
			if (Invoke(trial, [&] { return DecodeFrame(corpus[index], frame, AllRegistered); }) != DecodeStatus::Valid) {
				Fail(trial, "seed corpus frame was rejected");
			}
			CheckAccepted(trial, corpus[index], frame, AllRegistered);
			CheckKnownBad(trial, corpus[index], rng);
		}
		Expect(corpusTrial, Bytes(SnapMaxFrameBytes + 1, 0), DecodeStatus::SizeExceedsMax, AllRegistered);

		std::size_t accepted = 0;
		std::size_t rejected = 0;
		for (std::size_t iteration = 0; iteration < kIterations; ++iteration) {
			const Trial trial{ kDecodeFrame, a_seed, iteration };
			Bytes input = corpus[rng.Below(corpus.size())];
			for (std::size_t mutations = 1 + rng.Below(4); mutations > 0; --mutations) {
				if (rng.Below(2) == 0) {
					MutateBytes(input, rng);
				} else {
					CorruptLengthField(input, rng);
				}
			}
			const IsRegisteredFn registry = rng.Below(2) == 0 ? IsRegisteredFn{ AllRegistered } : IsRegisteredFn{ TransformOrNpc };
			DecodedFrame frame;
			const auto status = Invoke(trial, [&] { return DecodeFrame(input, frame, registry); });
			if (status != DecodeStatus::Valid) {
				++rejected;
				continue;
			}
			++accepted;
			CheckAccepted(trial, input, frame, registry);
		}
		std::cout << "decode_frame accepted=" << accepted << " rejected=" << rejected << '\n';
		if (accepted == 0) { Fail(corpusTrial, "no accepted frames were produced"); }
		if (rejected == 0) { Fail(corpusTrial, "no rejected frames were produced"); }
	}

	std::optional<CanonicalValue> SyntheticDecode(std::size_t a_size)
	{
		if (a_size % 3 == 0) {
			throw std::runtime_error{ "synthetic decoder failure" };
		}
		if (a_size % 3 == 1) {
			return std::nullopt;
		}
		return CanonicalValue{ a_size };
	}

	FamilyAdapter MakeAdapter(ConversionDirections a_directions)
	{
		FamilyAdapter adapter;
		adapter.metadata.legacy.name = "fuzzSnapshot";
		adapter.metadata.component.family = Family::Transform;
		adapter.metadata.supportedDirections = a_directions;
		adapter.legacyToCanonical = [](std::string_view a_payload) { return SyntheticDecode(a_payload.size()); };
		adapter.componentToCanonical = [](std::span<const std::uint8_t> a_payload) { return SyntheticDecode(a_payload.size()); };
		adapter.canonicalToLegacy = [](const CanonicalValue&) { return std::optional<std::string>{}; };
		adapter.canonicalToComponent = [](const CanonicalValue&) { return std::optional<Bytes>{}; };
		return adapter;
	}

	void ExpectOutcome(const Trial& a_trial, std::size_t a_size, const std::optional<CanonicalValue>& a_value)
	{
		const bool expectValue = a_size % 3 == 2;
		const auto* value = a_value ? std::any_cast<std::size_t>(&*a_value) : nullptr;
		if (a_value.has_value() != expectValue || (a_value.has_value() && (value == nullptr || *value != a_size))) {
			Fail(a_trial, "wrapper result differs from adapter result for payload size " + std::to_string(a_size));
		}
	}

	void FuzzComponentDecoders(std::uint64_t a_seed)
	{
		const auto adapter = MakeAdapter(AllConversionDirections);
		const auto noComponentDirection = MakeAdapter(LegacyToComponentDirection);
		const auto noLegacyDirection = MakeAdapter(ComponentToLegacyDirection);
		Rng rng{ std::mt19937_64{ a_seed } };
		for (std::size_t iteration = 0; iteration < kIterations; ++iteration) {
			const Trial componentTrial{ kDecodeComponent, a_seed, iteration };
			const Trial legacyTrial{ kDecodeLegacy, a_seed, iteration };
			Bytes payload(rng.Below(64));
			for (auto& byte : payload) {
				byte = RandomByte(rng);
			}
			const auto component = Invoke(componentTrial, [&] { return DecodeComponent(adapter, payload); });
			ExpectOutcome(componentTrial, payload.size(), component);
			if (DecodeComponent(noComponentDirection, payload).has_value()) {
				Fail(componentTrial, "decoded without a declared ComponentToLegacy direction");
			}

			const std::string text(payload.begin(), payload.end());
			const auto legacy = Invoke(legacyTrial, [&] { return DecodeLegacy(adapter, text); });
			ExpectOutcome(legacyTrial, text.size(), legacy);
			if (DecodeLegacy(noLegacyDirection, text).has_value()) {
				Fail(legacyTrial, "decoded without a declared LegacyToComponent direction");
			}
		}
	}
}

int main()
{
	std::uint64_t seed = kBaseSeed;
	FuzzDecodeFrame(seed++);
	FuzzComponentDecoders(seed++);
	std::cout << "validator fuzz snapshot passed: iterations_per_target=" << kIterations << " base_seed=" << kBaseSeed << '\n';
	return 0;
}
