#include "F4TDeathBagJournal.h"
#include "F4TDeathBagReceipts.h"
#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

using F4T::DeathBagJournal::Entry;
using F4T::DeathBagJournal::PlaceFailure;
using F4T::DeathBagJournal::State;
using F4T::DeathBagJournal::Unit;
using F4T::DeathBagReceipts::ReceiptChunkReader;
using F4T::DeathBagReceipts::ReceiptSet;

namespace
{
	std::string Fill(char a_character)
	{
		return std::string(64, a_character);
	}

	std::string DeathId(std::size_t a_index)
	{
		const auto digits = std::to_string(a_index);
		return std::string(32 - digits.size(), '0') + digits;
	}
}

int main()
{
	Unit single;
	single.reset = true;
	single.ops.push_back(F4T::DeathBagJournal::EntryOp("death:one", Entry{ std::uint64_t{ 7 }, true, false, false, 100 }));
	single.ops.push_back(F4T::DeathBagJournal::ProofOp("death:one", Fill('a')));
	const auto encoded = F4T::DeathBagJournal::Encode(1, single);
	assert(encoded && encoded->frames == 1);
	const auto replay = F4T::DeathBagJournal::Replay(encoded->bytes);
	assert(replay.valid && !replay.torn);
	assert(replay.committedBytes == encoded->bytes.size() && replay.nextSequence == 2);
	assert(replay.state.order.size() == 1 && replay.state.entries.at("death:one").added);
	assert(*replay.state.entries.at("death:one").baseline == std::uint64_t{ 7 } && replay.state.entries.at("death:one").addedAt == 100);
	assert(F4T::DeathBagReceipts::HasSaveProof(replay.state.withdrawalProofs, "death:one", Fill('a')));

	const auto incomplete = F4T::DeathBagJournal::Replay(encoded->bytes.substr(0, encoded->bytes.size() - 5));
	assert(incomplete.valid && incomplete.torn && incomplete.committedBytes == 0 && incomplete.nextSequence == 1);
	const auto nulTail = F4T::DeathBagJournal::Replay(encoded->bytes + std::string(16, '\0'));
	assert(nulTail.valid && nulTail.torn && nulTail.committedBytes == encoded->bytes.size());

	Unit removal;
	removal.ops.push_back(F4T::DeathBagJournal::RemoveOp("death:one"));
	const auto removalEncoded = F4T::DeathBagJournal::Encode(2, removal);
	assert(removalEncoded);
	const auto both = encoded->bytes + removalEncoded->bytes;
	const auto twoUnits = F4T::DeathBagJournal::Replay(both);
	assert(twoUnits.valid && !twoUnits.torn && twoUnits.nextSequence == 3 && twoUnits.state.entries.empty());
	auto corrupted = both;
	corrupted[12] = corrupted[12] == 'a' ? 'b' : 'a';
	assert(!F4T::DeathBagJournal::Replay(corrupted).valid);

	Unit large;
	large.reset = true;
	for (std::size_t i = 0; i < 300; ++i) {
		large.ops.push_back(F4T::DeathBagJournal::EntryOp(std::string(1000, 'x') + std::to_string(i), Entry{}));
	}
	const auto largeEncoded = F4T::DeathBagJournal::Encode(1, large);
	assert(largeEncoded && largeEncoded->frames == 2);
	const auto complete = F4T::DeathBagJournal::Replay(largeEncoded->bytes);
	assert(complete.valid && !complete.torn && complete.state.entries.size() == 300 && complete.nextSequence == 3);
	const auto firstPart = F4T::DeathBagHistory::DecodeTransaction(largeEncoded->bytes, 1);
	assert(firstPart.status == F4T::DeathBagHistory::DecodeStatus::Committed);
	const auto pending = F4T::DeathBagJournal::Replay(largeEncoded->bytes.substr(0, firstPart.consumed));
	assert(pending.valid && pending.torn && pending.committedBytes == 0 && pending.state.entries.empty());

	Unit reset;
	reset.reset = true;
	reset.ops.push_back(F4T::DeathBagJournal::ProofOp("death:two", Fill('b')));
	const auto resetEncoded = F4T::DeathBagJournal::Encode(2, reset);
	assert(resetEncoded);
	const auto cleared = F4T::DeathBagJournal::Replay(encoded->bytes + resetEncoded->bytes);
	assert(cleared.valid && !cleared.torn && cleared.state.entries.empty() && cleared.state.withdrawalProofs.contains("death:two"));
	assert(!cleared.state.withdrawalProofs.contains("death:one"));

	State unknown;
	Unit bogus;
	bogus.ops.push_back(nlohmann::json{ { "op", "bogus" } });
	assert(!F4T::DeathBagJournal::Apply(unknown, bogus));

	State state;
	state.order.push_back("death:one");
	state.entries["death:one"] = Entry{ std::uint64_t{ 7 }, true, false, false, 100 };
	state.withdrawalProofs["death:one"] = { Fill('a') };
	state.receiptBindings[Fill('c')] = Fill('d');
	state.placeFailures[DeathId(1)] = PlaceFailure{ DeathId(1), true, nlohmann::json::array(), 5 };
	const auto checkpoint = F4T::DeathBagJournal::Checkpoint(state);
	assert(checkpoint && checkpoint->reset);
	State restored;
	assert(F4T::DeathBagJournal::Apply(restored, *checkpoint));
	assert(restored.order == state.order && restored.entries.size() == 1 && restored.withdrawalProofs == state.withdrawalProofs);
	assert(restored.receiptBindings == state.receiptBindings && restored.placeFailures.size() == 1);
	state.entries["death:two"] = Entry{};
	assert(!F4T::DeathBagJournal::Checkpoint(state));

	std::size_t dropped = 0;
	assert(!F4T::DeathBagJournal::MigrateLegacy(nlohmann::json{ { "version", 4 }, { "grants", nlohmann::json::array() } }, 0, dropped));
	nlohmann::json manyHashes = nlohmann::json::array();
	for (std::size_t i = 0; i < 129; ++i) manyHashes.push_back(std::string(64 - std::to_string(i).size(), '0') + std::to_string(i));
	const std::string longOperation(200, 'o');
	nlohmann::json places = nlohmann::json::array();
	for (std::size_t i = 0; i < 65; ++i) places.push_back(nlohmann::json{ { "deathId", DeathId(i) }, { "restored", true }, { "remaining", nlohmann::json::array() }, { "createdAt", 5 } });
	places.push_back(nlohmann::json{ { "deathId", "bad" }, { "restored", true }, { "remaining", nlohmann::json::array() } });
	const nlohmann::json legacy{ { "version", 4 }, { "grants", nlohmann::json::array({ longOperation }) }, { "withdrawalProofs", nlohmann::json{ { longOperation, manyHashes } } }, { "receiptBindings", nlohmann::json::object() }, { "placeFailures", places } };
	const auto migrated = F4T::DeathBagJournal::MigrateLegacy(legacy, 0, dropped);
	assert(migrated && dropped == 1);
	assert(migrated->entries.contains(longOperation) && migrated->withdrawalProofs.at(longOperation).size() == 129);
	assert(migrated->placeFailures.size() == 65);

	ReceiptSet receipts;
	for (std::size_t i = 0; i < 1000; ++i) assert(receipts.Insert(std::string(100, 'a') + std::to_string(i)));
	const auto payload = receipts.Encode();
	const auto chunks = F4T::DeathBagReceipts::EncodeChunks(payload);
	assert(chunks.size() >= 2 && chunks.size() == (payload.size() + F4T::DeathBagReceipts::MaximumChunkBytes - 1) / F4T::DeathBagReceipts::MaximumChunkBytes);
	ReceiptChunkReader reader;
	for (const auto& chunk : chunks) assert(reader.Add(chunk));
	const auto decoded = reader.Decode();
	assert(decoded && decoded->Encode() == payload);
	ReceiptChunkReader reversed;
	for (auto it = chunks.rbegin(); it != chunks.rend(); ++it) assert(reversed.Add(*it));
	const auto reversedDecoded = reversed.Decode();
	assert(reversedDecoded && reversedDecoded->Encode() == payload);
	ReceiptChunkReader missing;
	assert(missing.Add(chunks.front()));
	assert(!missing.Decode());
	auto damaged = chunks.front();
	damaged.back() = damaged.back() == 'a' ? 'b' : 'a';
	ReceiptChunkReader conflict;
	assert(conflict.Add(chunks.front()));
	assert(!conflict.Add(damaged));
	assert(!conflict.Decode());
	ReceiptChunkReader tampered;
	assert(tampered.Add(damaged));
	for (std::size_t i = 1; i < chunks.size(); ++i) assert(tampered.Add(chunks[i]));
	assert(!tampered.Decode());
	ReceiptChunkReader empty;
	assert(!empty.Add(std::string_view{}));

	auto zeroedTail = encoded->bytes;
	std::fill(zeroedTail.begin() + 20, zeroedTail.begin() + 40, '\0');
	const auto tornTail = F4T::DeathBagJournal::Replay(zeroedTail);
	assert(tornTail.valid && tornTail.torn && tornTail.committedBytes == 0);
	auto zeroedMiddle = both;
	std::fill(zeroedMiddle.begin() + 20, zeroedMiddle.begin() + 40, '\0');
	assert(!F4T::DeathBagJournal::Replay(zeroedMiddle).valid);
	assert(!F4T::DeathBagJournal::Replay(std::string(64, '\0') + removalEncoded->bytes).valid);
	const auto zeroFillMiddle = F4T::DeathBagJournal::Replay(encoded->bytes + std::string(64, '\0') + removalEncoded->bytes);
	assert(!zeroFillMiddle.valid && zeroFillMiddle.committedBytes == encoded->bytes.size());

	const nlohmann::json emptyPart{ { "ops", nlohmann::json::array() }, { "part", 0 }, { "parts", 1 }, { "reset", false } };
	const auto gapFrame = F4T::DeathBagHistory::EncodeTransaction(3, emptyPart);
	const auto trailingFrame = F4T::DeathBagHistory::EncodeTransaction(4, emptyPart);
	assert(gapFrame && trailingFrame);
	assert(!F4T::DeathBagJournal::Replay(encoded->bytes + *gapFrame + *trailingFrame).valid);
	assert(!F4T::DeathBagJournal::Replay(encoded->bytes + *gapFrame).valid);
	auto overclaimed = *gapFrame;
	overclaimed.replace(0, 8, "00040000");
	assert(!F4T::DeathBagJournal::Replay(encoded->bytes + overclaimed + std::string(F4T::DeathBagHistory::MaximumTransactionBytes + F4T::DeathBagHistory::FrameOverhead + 1, 'x')).valid);
	const auto firstOfThree = F4T::DeathBagHistory::EncodeTransaction(1, nlohmann::json{ { "ops", nlohmann::json::array() }, { "part", 0 }, { "parts", 3 }, { "reset", true } });
	const auto skippedPart = F4T::DeathBagHistory::EncodeTransaction(2, nlohmann::json{ { "ops", nlohmann::json::array() }, { "part", 2 }, { "parts", 3 }, { "reset", true } });
	assert(firstOfThree && skippedPart);
	assert(!F4T::DeathBagJournal::Replay(*firstOfThree + *skippedPart).valid);

	State partial;
	partial.order.push_back("death:one");
	partial.entries["death:one"] = Entry{};
	Unit mixed;
	mixed.ops.push_back(F4T::DeathBagJournal::RemoveOp("death:one"));
	mixed.ops.push_back(nlohmann::json{ { "op", "bogus" } });
	assert(!F4T::DeathBagJournal::Apply(partial, mixed));
	assert(partial.order.size() == 1 && partial.entries.contains("death:one"));
	mixed.reset = true;
	assert(!F4T::DeathBagJournal::Apply(partial, mixed) && partial.entries.contains("death:one"));

	State guarded;
	Unit oversized;
	oversized.ops.push_back(F4T::DeathBagJournal::EntryOp(std::string(F4T::DeathBagJournal::MaximumPartOpsBytes, 'o'), Entry{}));
	State probe;
	assert(F4T::DeathBagJournal::Apply(probe, oversized));
	assert(!F4T::DeathBagJournal::Encode(1, oversized));
	assert(!F4T::DeathBagJournal::ApplyEncodable(guarded, oversized, 1) && guarded.order.empty() && guarded.entries.empty());

	for (const int version : { 1, 2, 3 }) {
		std::size_t droppedLegacy = 0;
		const auto legacyState = F4T::DeathBagJournal::MigrateLegacy(nlohmann::json{ { "version", version }, { "grants", nlohmann::json::array({ "death:a", "death:b" }) } }, 0, droppedLegacy);
		assert(legacyState && droppedLegacy == 0 && legacyState->order.size() == 2 && legacyState->entries.size() == 2);
		assert(!legacyState->entries.at("death:a").added && !legacyState->entries.at("death:a").baseline);
	}

	const auto pendingStacks = nlohmann::json::array({ nlohmann::json{ { "itemKey", "fallout4.esm:00000002" }, { "count", 2u }, { "metadata", nullptr } } });
	Unit pendingUnit;
	pendingUnit.ops.push_back(F4T::DeathBagJournal::PendOp(F4T::DeathBagJournal::PendingPlace{ DeathId(9), "pending", pendingStacks, 500 }));
	pendingUnit.ops.push_back(F4T::DeathBagJournal::PendOp(F4T::DeathBagJournal::PendingPlace{ DeathId(10), "placed", pendingStacks, 501 }));
	pendingUnit.ops.push_back(F4T::DeathBagJournal::ForgetPendOp(DeathId(10)));
	const auto pendingEncoded = F4T::DeathBagJournal::Encode(1, pendingUnit);
	assert(pendingEncoded && pendingEncoded->frames == 1);
	const auto pendingReplay = F4T::DeathBagJournal::Replay(pendingEncoded->bytes);
	assert(pendingReplay.valid && !pendingReplay.torn && pendingReplay.state.pendingPlaces.size() == 1);
	assert(pendingReplay.state.pendingPlaces.at(DeathId(9)).phase == "pending" && pendingReplay.state.pendingPlaces.at(DeathId(9)).stacks == pendingStacks);
	const auto pendingCheckpoint = F4T::DeathBagJournal::Checkpoint(pendingReplay.state);
	assert(pendingCheckpoint && F4T::DeathBagJournal::Encode(1, *pendingCheckpoint));
	State reloaded;
	assert(F4T::DeathBagJournal::Apply(reloaded, *pendingCheckpoint) && reloaded.pendingPlaces.size() == 1);
	Unit badPhase;
	badPhase.ops.push_back(nlohmann::json{ { "op", "pend" }, { "deathId", DeathId(11) }, { "phase", "elsewhere" }, { "stacks", pendingStacks }, { "createdAt", 1 } });
	State rejected;
	assert(!F4T::DeathBagJournal::Apply(rejected, badPhase) && rejected.pendingPlaces.empty());
	Unit badStacks;
	badStacks.ops.push_back(nlohmann::json{ { "op", "pend" }, { "deathId", DeathId(12) }, { "phase", "pending" }, { "stacks", nlohmann::json::object() }, { "createdAt", 1 } });
	assert(!F4T::DeathBagJournal::Apply(rejected, badStacks) && rejected.pendingPlaces.empty());

	Unit placing;
	placing.ops.push_back(F4T::DeathBagJournal::PendOp(F4T::DeathBagJournal::PendingPlace{ DeathId(13), "placing", pendingStacks, 700 }));
	const auto placingEncoded = F4T::DeathBagJournal::Encode(1, placing);
	assert(placingEncoded && placingEncoded->frames == 1);
	const auto placingReplay = F4T::DeathBagJournal::Replay(placingEncoded->bytes);
	assert(placingReplay.valid && !placingReplay.torn && placingReplay.state.pendingPlaces.size() == 1);
	assert(placingReplay.state.pendingPlaces.at(DeathId(13)).phase == "placing" && placingReplay.state.pendingPlaces.at(DeathId(13)).createdAt == 700);
	const auto placingCheckpoint = F4T::DeathBagJournal::Checkpoint(placingReplay.state);
	assert(placingCheckpoint);
	State placingReloaded;
	assert(F4T::DeathBagJournal::Apply(placingReloaded, *placingCheckpoint) && placingReloaded.pendingPlaces.at(DeathId(13)).phase == "placing");
	Unit unknownPhase;
	unknownPhase.ops.push_back(nlohmann::json{ { "op", "pend" }, { "deathId", DeathId(14) }, { "phase", "placingSoon" }, { "stacks", pendingStacks }, { "createdAt", 1 } });
	State strictPhase;
	assert(!F4T::DeathBagJournal::Apply(strictPhase, unknownPhase) && strictPhase.pendingPlaces.empty());
}
