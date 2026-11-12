#include "F4TMutationRing.h"

#include <array>
#include <atomic>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <span>
#include <thread>
#include <vector>

// source:co-core/include/F4TMutationRing.h CO-defined dump fixture entries
namespace
{
	void AppendLittleEndian(std::vector<std::byte>& a_out, std::uint64_t a_value, std::size_t a_width)
	{
		for (std::size_t index = 0; index < a_width; ++index) {
			a_out.push_back(static_cast<std::byte>((a_value >> (8 * index)) & 0xFFU));
		}
	}

	void WriteFixture(const char* a_path)
	{
		using namespace F4T::MutationRing;

		std::array<Entry, 8> entries{};
		for (std::size_t index = 0; index < entries.size(); ++index) {
			const auto seq = static_cast<std::uint64_t>(index + 1);
			entries[index] = Entry{
				seq,
				seq * 1000,
				static_cast<std::uint16_t>(index % 4),
				static_cast<std::uint32_t>(0x14 + index),
				0x7FF000000000 + seq * 0x10,
				static_cast<std::uint32_t>(seq * 3)
			};
		}

		std::vector<std::byte> bytes(DumpHeaderSize + entries.size() * EntryWireSize);
		const auto encoded = Encode(entries, bytes);
		assert(encoded == bytes.size());
		for (const char letter : { 'C', 'R', 'F', 'T' }) {
			bytes.push_back(static_cast<std::byte>(letter));
		}
		AppendLittleEndian(bytes, 0x00007FF6A1B2C3D4, 8);
		AppendLittleEndian(bytes, 0x0000000000000008, 8);

		std::ofstream output(a_path, std::ios::binary);
		assert(output.is_open());
		output.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
		assert(output.good());
	}

	// constraint: source:co-core/include/F4TMutationRing.h CO-defined formId/address/aux triple
	bool ConsistentTriple(const F4T::MutationRing::Entry& a_entry, std::uint32_t a_formId, std::uint64_t a_address, std::uint32_t a_aux)
	{
		return a_entry.formId == a_formId && a_entry.address == a_address && a_entry.aux == a_aux;
	}

	std::atomic_bool g_stall{ true };
	std::atomic_bool g_claimed{ false };

	void StallFirstClaim()
	{
		if (g_claimed.exchange(true, std::memory_order_acq_rel)) {
			return;
		}
		while (g_stall.load(std::memory_order_acquire)) {
			std::this_thread::yield();
		}
	}
}

namespace F4T::MutationRing
{
	void SetTestAfterClaimHook(void (*a_hook)());
}

int main(int argc, char** argv)
{
	using namespace F4T::MutationRing;

	constexpr std::uint64_t kSingleWrites = 5000;
	constexpr std::uint64_t kThreadWrites = 10000;
	constexpr std::uint32_t kSlowForm = 0xA11A11A1U;
	constexpr std::uint64_t kSlowAddress = 0xB22B22B22B22B22BULL;
	constexpr std::uint32_t kSlowAux = 0xC33C33C3U;
	constexpr std::uint32_t kFastForm = 0x11111111U;
	constexpr std::uint64_t kFastAddress = 0x2222222222222222ULL;
	constexpr std::uint32_t kFastAux = 0x33333333U;

	SetEnabled(true);
	assert(IsEnabled());

	g_stall.store(true, std::memory_order_relaxed);
	g_claimed.store(false, std::memory_order_relaxed);
	SetTestAfterClaimHook(&StallFirstClaim);
	std::thread stalledWriter([] {
		Record(MutationSite::kTimeState, kSlowForm, kSlowAddress, kSlowAux);
	});
	while (!g_claimed.load(std::memory_order_acquire)) {
		std::this_thread::yield();
	}
	for (std::size_t index = 0; index < Capacity + 8; ++index) {
		Record(MutationSite::kWeatherState, kFastForm, kFastAddress, kFastAux);
	}
	g_stall.store(false, std::memory_order_release);
	stalledWriter.join();
	SetTestAfterClaimHook(nullptr);

	std::vector<Entry> wrapSnapshot(Capacity);
	const auto wrapCount = Snapshot(wrapSnapshot);
	assert(wrapCount > 0 && wrapCount <= Capacity);
	std::size_t slowCount = 0;
	std::uint64_t minSeq = UINT64_MAX;
	for (std::size_t index = 0; index < wrapCount; ++index) {
		const auto& entry = wrapSnapshot[index];
		const auto slowMatch = ConsistentTriple(entry, kSlowForm, kSlowAddress, kSlowAux);
		const auto fastMatch = ConsistentTriple(entry, kFastForm, kFastAddress, kFastAux);
		assert(slowMatch || fastMatch);
		if (entry.seq < minSeq) {
			minSeq = entry.seq;
		}
		if (slowMatch) {
			++slowCount;
			assert(entry.seq > Capacity);
		}
		if (index > 0) {
			assert(entry.seq > wrapSnapshot[index - 1].seq);
		}
	}
	assert(slowCount == 1);
	assert(minSeq > 8);
	std::printf("mutation ring wrap-race passed: slowCount=%zu minSeq=%llu\n", slowCount, static_cast<unsigned long long>(minSeq));

	for (std::uint64_t index = 1; index <= kSingleWrites; ++index) {
		Record(MutationSite::kWeatherState, static_cast<std::uint32_t>(index), 0x1000 + index, static_cast<std::uint32_t>(index * 2));
	}

	std::vector<Entry> snapshot(Capacity);
	assert(Snapshot(snapshot) == Capacity);
	std::array<bool, kSingleWrites + 1> seenForms{};
	for (std::size_t index = 0; index < Capacity; ++index) {
		const auto& entry = snapshot[index];
		assert(entry.formId >= 1 && entry.formId <= kSingleWrites);
		assert(entry.address == 0x1000 + entry.formId);
		assert(entry.aux == entry.formId * 2);
		assert(entry.site == static_cast<std::uint16_t>(MutationSite::kWeatherState));
		seenForms[entry.formId] = true;
		if (index > 0) {
			assert(entry.seq > snapshot[index - 1].seq);
		}
	}
	for (std::uint64_t formId = kSingleWrites - Capacity + 1; formId <= kSingleWrites; ++formId) {
		assert(seenForms[formId]);
	}

	SetEnabled(false);
	Record(MutationSite::kTimeState, 1, 1, 1);
	SetEnabled(true);
	assert(Snapshot(snapshot) == Capacity);
	const auto disabledSeq = snapshot.back().seq;

	std::vector<std::byte> bytes(MaxDumpSize);
	const auto size = Encode(snapshot, bytes);
	assert(size == MaxDumpSize);
	assert(bytes[0] == std::byte{ 'C' } && bytes[1] == std::byte{ 'O' } && bytes[2] == std::byte{ 'R' } && bytes[3] == std::byte{ 'G' });
	assert(bytes[4] == std::byte{ 1 } && bytes[5] == std::byte{ 0 });
	assert(bytes[8] == std::byte{ 0 } && bytes[9] == std::byte{ 0x10 });
	assert(bytes[DumpHeaderSize] == std::byte{ static_cast<unsigned char>(snapshot.front().seq & 0xFFU) });
	assert(bytes[DumpHeaderSize + 1] == std::byte{ static_cast<unsigned char>((snapshot.front().seq >> 8) & 0xFFU) });

	std::vector<Entry> decoded;
	const std::span<const std::byte> dump{ bytes.data(), size };
	assert(Decode(dump, decoded));
	assert(decoded.size() == Capacity);
	for (std::size_t index = 0; index < Capacity; ++index) {
		assert(decoded[index].seq == snapshot[index].seq);
		assert(decoded[index].tick == snapshot[index].tick);
		assert(decoded[index].site == snapshot[index].site);
		assert(decoded[index].formId == snapshot[index].formId);
		assert(decoded[index].address == snapshot[index].address);
		assert(decoded[index].aux == snapshot[index].aux);
	}

	std::vector<std::byte> mutated(bytes.begin(), bytes.begin() + static_cast<std::ptrdiff_t>(size));
	mutated[4] = std::byte{ 2 };
	assert(!Decode(mutated, decoded));
	mutated[4] = std::byte{ 1 };
	mutated[0] = std::byte{ 'X' };
	assert(!Decode(mutated, decoded));
	assert(!Decode(dump.first(size - 1), decoded));
	assert(!Decode(dump.first(DumpHeaderSize - 1), decoded));

	auto writer = [](std::uint32_t a_base) {
		for (std::uint32_t index = 0; index < kThreadWrites; ++index) {
			const auto formId = a_base + index;
			Record(MutationSite::kPositionCorrection, formId, formId, formId);
		}
	};
	std::thread first(writer, 1U);
	std::thread second(writer, 100000U);
	first.join();
	second.join();

	std::vector<Entry> threaded(Capacity);
	const auto threadedCount = Snapshot(threaded);
	assert(threadedCount > 0 && threadedCount <= Capacity);
	for (std::size_t index = 0; index < threadedCount; ++index) {
		const auto& entry = threaded[index];
		if (index > 0) {
			assert(entry.seq > threaded[index - 1].seq);
		}
		assert(entry.formId == entry.address && entry.formId == entry.aux);
		assert(entry.site == static_cast<std::uint16_t>(MutationSite::kPositionCorrection) ||
			entry.site == static_cast<std::uint16_t>(MutationSite::kWeatherState) ||
			entry.site == static_cast<std::uint16_t>(MutationSite::kTimeState));
		if (entry.site == static_cast<std::uint16_t>(MutationSite::kPositionCorrection)) {
			assert((entry.formId >= 1 && entry.formId < 1 + kThreadWrites) ||
				(entry.formId >= 100000 && entry.formId < 100000 + kThreadWrites));
		}
	}
	assert(threaded[threadedCount - 1].seq > disabledSeq);

	if (argc > 1) {
		WriteFixture(argv[1]);
	}

	SetEnabled(false);
	return 0;
}
