#include "F4TPayloadCodec.h"
#include "F4TSnapshotDeltaWire.h"

#include <zstd.h>

#include <bit>

namespace F4T::PayloadCodec
{
	namespace
	{
		std::vector<std::uint8_t> EncodeRaw(std::span<const std::uint8_t> a_payload)
		{
			std::vector<std::uint8_t> out;
			if (!a_payload.empty() && (a_payload.front() == FlagRaw || a_payload.front() == FlagZstd)) {
				out.push_back(FlagRaw);
			}
			out.insert(out.end(), a_payload.begin(), a_payload.end());
			return out;
		}
	}

	std::vector<std::uint8_t> Encode(std::span<const std::uint8_t> a_payload)
	{
		if (a_payload.size() <= CompressThresholdBytes) {
			return EncodeRaw(a_payload);
		}

		const auto bound = ZSTD_compressBound(a_payload.size());
		if (ZSTD_isError(bound) || bound == 0) {
			return EncodeRaw(a_payload);
		}

		std::vector<std::uint8_t> compressed(bound);
		const auto written = ZSTD_compress(
			compressed.data(),
			compressed.size(),
			a_payload.data(),
			a_payload.size(),
			3);
		if (ZSTD_isError(written) || written == 0 || written >= a_payload.size()) {
			return EncodeRaw(a_payload);
		}

		compressed.resize(written);
		std::vector<std::uint8_t> out;
		out.reserve(1 + compressed.size());
		out.push_back(FlagZstd);
		out.insert(out.end(), compressed.begin(), compressed.end());
		return out;
	}

	namespace
	{
		std::optional<std::vector<std::uint8_t>> DecodeUnknownSize(std::span<const std::uint8_t> a_frame, std::size_t a_maxDecoded)
		{
			const auto stream = ZSTD_createDStream();
			if (stream == nullptr) {
				return std::nullopt;
			}
			constexpr auto maxWindowLog = static_cast<int>(std::bit_width(MaximumDecompressedBytes - 1));
			if (ZSTD_isError(ZSTD_DCtx_setParameter(stream, ZSTD_d_windowLogMax, maxWindowLog))) {
				ZSTD_freeDStream(stream);
				return std::nullopt;
			}
			std::vector<std::uint8_t> out;
			std::vector<std::uint8_t> chunk(ZSTD_DStreamOutSize());
			ZSTD_inBuffer input{ a_frame.data(), a_frame.size(), 0 };
			std::size_t result = 1;
			while (result != 0) {
				const auto consumedBefore = input.pos;
				ZSTD_outBuffer output{ chunk.data(), chunk.size(), 0 };
				result = ZSTD_decompressStream(stream, &output, &input);
				if (ZSTD_isError(result) || output.pos > a_maxDecoded - out.size() ||
					(result != 0 && output.pos == 0 && input.pos == consumedBefore)) {
					ZSTD_freeDStream(stream);
					return std::nullopt;
				}
				out.insert(out.end(), chunk.data(), chunk.data() + output.pos);
			}
			ZSTD_freeDStream(stream);
			if (input.pos != input.size || out.empty()) {
				return std::nullopt;
			}
			return out;
		}
	}

	std::optional<std::vector<std::uint8_t>> Decode(std::span<const std::uint8_t> a_framed, std::size_t a_maxDecoded)
	{
		if (a_framed.empty()) {
			return std::nullopt;
		}

		const auto flag = a_framed.front();
		if (flag != FlagRaw && flag != FlagZstd) {
			if (a_framed.size() > a_maxDecoded) {
				return std::nullopt;
			}
			return std::vector<std::uint8_t>{ a_framed.begin(), a_framed.end() };
		}

		const auto body = a_framed.subspan(1);
		if (flag == FlagRaw) {
			if (body.size() > a_maxDecoded) {
				return std::nullopt;
			}
			return std::vector<std::uint8_t>{ body.begin(), body.end() };
		}

		const auto declared = ZSTD_getFrameContentSize(body.data(), body.size());
		if (declared == ZSTD_CONTENTSIZE_ERROR) {
			F4T::Replication::DecodedFrame snapshot;
			if (a_framed.size() <= a_maxDecoded &&
				F4T::Replication::DecodeFrame(a_framed, snapshot, [](auto) { return true; }) == F4T::Replication::DecodeStatus::Valid) {
				return std::vector<std::uint8_t>{ a_framed.begin(), a_framed.end() };
			}
			return std::nullopt;
		}
		if (declared == 0) return std::nullopt;
		if (declared == ZSTD_CONTENTSIZE_UNKNOWN) {
			return DecodeUnknownSize(body, a_maxDecoded);
		}
		if (declared > a_maxDecoded) {
			return std::nullopt;
		}

		std::vector<std::uint8_t> out(static_cast<std::size_t>(declared));
		const auto written = ZSTD_decompress(out.data(), out.size(), body.data(), body.size());
		if (ZSTD_isError(written) || written != out.size()) {
			return std::nullopt;
		}
		return out;
	}
}
