#include "F4TPayloadCodec.h"
#include "F4TSnapshotDeltaWire.h"

#include <zstd.h>

#include <cassert>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

int main()
{
	using namespace F4T::PayloadCodec;

	{
		const std::vector<std::uint8_t> small{ '{', '"', 'a', '"', ':', '1', '}' };
		const auto framed = Encode(small);
		assert(framed == small);
		const auto decoded = Decode(framed, MaximumDecompressedBytes);
		assert(decoded);
		assert(*decoded == small);
	}

	{
		std::vector<std::uint8_t> large(CompressThresholdBytes + 1, static_cast<std::uint8_t>('a'));
		const auto framed = Encode(large);
		assert(!framed.empty());
		assert(framed.front() == FlagZstd || framed == large);
		if (framed.front() == FlagZstd) {
			assert(framed.size() < large.size() + 1);
		}
		const auto decoded = Decode(framed, MaximumDecompressedBytes);
		assert(decoded);
		assert(*decoded == large);
	}

	{
		const std::string legacy = R"({"type":"hello"})";
		const std::span<const std::uint8_t> bytes{
			reinterpret_cast<const std::uint8_t*>(legacy.data()),
			legacy.size()
		};
		const auto decoded = Decode(bytes, MaximumDecompressedBytes);
		assert(decoded);
		assert(std::string(decoded->begin(), decoded->end()) == legacy);
	}

	{
		std::vector<std::uint8_t> bomb{ FlagZstd };
		const std::vector<std::uint8_t> junk(64, 0);
		bomb.insert(bomb.end(), junk.begin(), junk.end());
		assert(!Decode(bomb, MaximumDecompressedBytes));
	}

	{
		std::vector<std::uint8_t> rawPrefixed{ FlagRaw, 'x', 'y' };
		const auto decoded = Decode(rawPrefixed, MaximumDecompressedBytes);
		assert(decoded);
		assert(*decoded == (std::vector<std::uint8_t>{ 'x', 'y' }));
		assert(!Decode(rawPrefixed, 1));
	}

	{
		const std::vector<std::uint8_t> plain(100, static_cast<std::uint8_t>('p'));
		assert(!Decode(plain, 99));
		assert(Decode(plain, 100));
	}

	{
		const std::vector<std::uint8_t> large(CompressThresholdBytes * 4, static_cast<std::uint8_t>('a'));
		const auto framed = Encode(large);
		assert(framed.front() == FlagZstd);
		assert(!Decode(framed, large.size() - 1));
		const auto decoded = Decode(framed, large.size());
		assert(decoded);
		assert(*decoded == large);
	}

	{
		const std::vector<std::uint8_t> source(64 * 1024, static_cast<std::uint8_t>('q'));
		std::vector<std::uint8_t> compressed(ZSTD_compressBound(source.size()));
		const auto context = ZSTD_createCCtx();
		assert(context != nullptr);
		assert(!ZSTD_isError(ZSTD_CCtx_setParameter(context, ZSTD_c_contentSizeFlag, 0)));
		const auto written = ZSTD_compress2(context, compressed.data(), compressed.size(), source.data(), source.size());
		ZSTD_freeCCtx(context);
		assert(!ZSTD_isError(written));
		compressed.resize(written);
		assert(ZSTD_getFrameContentSize(compressed.data(), compressed.size()) == ZSTD_CONTENTSIZE_UNKNOWN);
		std::vector<std::uint8_t> framed{ FlagZstd };
		framed.insert(framed.end(), compressed.begin(), compressed.end());
		assert(!Decode(framed, 1024));
		const auto decoded = Decode(framed, source.size());
		assert(decoded);
		assert(*decoded == source);
		auto largeWindow = framed;
		assert(largeWindow.size() > 6);
		largeWindow[6] = 0x60;
		assert(Decode(largeWindow, source.size()) == source);
		largeWindow[6] = 0x70;
		assert(ZSTD_getFrameContentSize(largeWindow.data() + 1, largeWindow.size() - 1) == ZSTD_CONTENTSIZE_UNKNOWN);
		assert(!Decode(largeWindow, source.size()));
	}

	{
		const auto snapshot = F4T::Replication::EncodeBaseline({}, 1);
		assert(snapshot && snapshot->front() == FlagZstd);
		const auto framed = Encode(*snapshot);
		assert(framed.front() == FlagRaw);
		assert(Decode(framed, MaximumDecompressedBytes) == snapshot);
		assert(Decode(*snapshot, MaximumDecompressedBytes) == snapshot);
		assert(!Decode(*snapshot, snapshot->size() - 1));
		auto malformed = *snapshot;
		malformed.pop_back();
		assert(!Decode(malformed, MaximumDecompressedBytes));
		malformed = *snapshot;
		malformed.push_back(0);
		assert(!Decode(malformed, MaximumDecompressedBytes));
		malformed = *snapshot;
		malformed[3] = 1;
		assert(!Decode(malformed, MaximumDecompressedBytes));
		const std::vector<std::uint8_t> zero{ FlagRaw, 7 };
		assert(Decode(Encode(zero), MaximumDecompressedBytes) == zero);
	}

	{
		const std::vector<std::uint8_t> source(16384, static_cast<std::uint8_t>('z'));
		const auto framed = Encode(source);
		assert(framed.front() == FlagZstd);
		for (std::size_t size = 1; size < framed.size(); ++size) {
			assert(!Decode(std::span<const std::uint8_t>{ framed.data(), size }, source.size()));
		}
		assert(Decode(framed, source.size()) == source);
		assert(!Decode(framed, 0));
	}

	assert(!Decode({}, MaximumDecompressedBytes));
	return 0;
}
