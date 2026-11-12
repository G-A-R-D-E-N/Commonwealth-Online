#include "F4TModpackChunks.h"
#include "F4TContentManifest.h"

#include <cassert>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace
{
	using namespace F4T::ModpackChunks;

	std::string Manifest(std::size_t a_size)
	{
		return std::string(a_size, 'm');
	}

	void TestBase64RoundTrip()
	{
		for (const auto size : { 1, 2, 3, 4, 5, 64, 1000 }) {
			const auto text = Manifest(size);
			const auto encoded = EncodeBase64(text);
			const auto decoded = DecodeBase64(encoded, size);
			assert(decoded.has_value());
			assert(*decoded == text);
		}
		assert(!DecodeBase64("A", 10).has_value());
		assert(!DecodeBase64("====", 10).has_value());
		assert(!DecodeBase64(EncodeBase64(Manifest(100)), 10).has_value());
	}

	void TestSplitBoundaries()
	{
		const auto exact = Split(Manifest(kChunkPayloadBytes));
		assert(exact.has_value());
		assert(Describe(Manifest(kChunkPayloadBytes))->chunkCount == 1);
		const auto over = Split(Manifest(kChunkPayloadBytes + 1));
		assert(over.has_value());
		assert(Describe(Manifest(kChunkPayloadBytes + 1))->chunkCount == 2);
		const auto last = DecodeBase64(over->back().payloadBase64, kChunkPayloadBytes);
		assert(last.has_value() && last->size() == 1);
		const auto largest = Split(Manifest(kMaximumTotalBytes));
		assert(largest.has_value());
		assert(largest->size() == kMaximumChunks);
		assert(!Describe(Manifest(kMaximumTotalBytes + 1)).has_value());
	}

	void TestRoundTrip()
	{
		const auto manifest = Manifest(kChunkPayloadBytes * 2 + 17);
		const auto description = Describe(manifest);
		assert(description.has_value());
		const auto chunks = Split(manifest);
		assert(chunks.has_value());
		Reassembler reassembler(*description);
		for (const auto& chunk : *chunks) {
			assert(reassembler.Add(chunk.index, chunk.payloadBase64) == AddResult::accepted);
		}
		assert(reassembler.Complete());
		assert(reassembler.Assemble() == manifest);
	}

	void TestMissingChunk()
	{
		const auto manifest = Manifest(kChunkPayloadBytes + 5);
		const auto description = *Describe(manifest);
		const auto chunks = *Split(manifest);
		Reassembler reassembler(description);
		assert(reassembler.Add(0, chunks[0].payloadBase64) == AddResult::accepted);
		assert(!reassembler.Complete());
		assert(!reassembler.Assemble().has_value());
	}

	void TestDuplicateChunk()
	{
		const auto manifest = Manifest(kChunkPayloadBytes + 5);
		const auto description = *Describe(manifest);
		const auto chunks = *Split(manifest);
		Reassembler reassembler(description);
		assert(reassembler.Add(0, chunks[0].payloadBase64) == AddResult::accepted);
		assert(reassembler.Add(0, chunks[0].payloadBase64) == AddResult::duplicate);
	}

	void TestOutOfOrderChunk()
	{
		const auto manifest = Manifest(kChunkPayloadBytes + 5);
		const auto description = *Describe(manifest);
		const auto chunks = *Split(manifest);
		Reassembler reassembler(description);
		assert(reassembler.Add(1, chunks[1].payloadBase64) == AddResult::outOfOrder);
	}

	void TestBadIndexAndOversize()
	{
		const auto manifest = Manifest(kChunkPayloadBytes + 5);
		const auto description = *Describe(manifest);
		const auto chunks = *Split(manifest);
		Reassembler reassembler(description);
		assert(reassembler.Add(9, chunks[0].payloadBase64) == AddResult::invalid);
		assert(reassembler.Add(0, EncodeBase64(Manifest(kChunkPayloadBytes + 1))) == AddResult::oversize);
	}

	void TestBadHashAndTotal()
	{
		const auto manifest = Manifest(kChunkPayloadBytes + 5);
		auto description = *Describe(manifest);
		const auto chunks = *Split(manifest);
		description.sha256 = std::string(64, 'a');
		Reassembler wrongHash(description);
		assert(wrongHash.Add(0, chunks[0].payloadBase64) == AddResult::accepted);
		assert(wrongHash.Add(1, chunks[1].payloadBase64) == AddResult::accepted);
		assert(wrongHash.Complete());
		assert(!wrongHash.Assemble().has_value());
		auto wrongTotal = *Describe(manifest);
		wrongTotal.totalBytes += 1;
		Reassembler badTotal(wrongTotal);
		assert(badTotal.Add(0, chunks[0].payloadBase64) == AddResult::accepted);
		assert(badTotal.Add(1, chunks[1].payloadBase64) == AddResult::oversize);
		auto wrongCount = *Describe(manifest);
		wrongCount.chunkCount += 1;
		Reassembler badCount(wrongCount);
		assert(badCount.Add(0, chunks[0].payloadBase64) == AddResult::invalid);
		Reassembler zeroTotal(Description{ 0, 0, std::string(64, 'a') });
		assert(!zeroTotal.Complete());
		assert(!zeroTotal.Assemble().has_value());
	}
}

int main()
{
	TestBase64RoundTrip();
	TestSplitBoundaries();
	TestRoundTrip();
	TestMissingChunk();
	TestDuplicateChunk();
	TestOutOfOrderChunk();
	TestBadIndexAndOversize();
	TestBadHashAndTotal();
	return 0;
}
