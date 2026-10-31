#include "F4TModpackChunks.h"

#include "F4TContentManifest.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace
{
	constexpr char kAlphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

	int DecodeCharacter(char a_character) noexcept
	{
		if (a_character >= 'A' && a_character <= 'Z') {
			return a_character - 'A';
		}
		if (a_character >= 'a' && a_character <= 'z') {
			return a_character - 'a' + 26;
		}
		if (a_character >= '0' && a_character <= '9') {
			return a_character - '0' + 52;
		}
		if (a_character == '+') {
			return 62;
		}
		if (a_character == '/') {
			return 63;
		}
		return -1;
	}
}

namespace F4T::ModpackChunks
{
	std::string EncodeBase64(std::string_view a_bytes)
	{
		std::string text;
		text.reserve(((a_bytes.size() + 2) / 3) * 4);
		for (std::size_t index = 0; index < a_bytes.size(); index += 3) {
			const auto first = static_cast<unsigned char>(a_bytes[index]);
			const auto second = index + 1 < a_bytes.size() ? static_cast<unsigned char>(a_bytes[index + 1]) : 0;
			const auto third = index + 2 < a_bytes.size() ? static_cast<unsigned char>(a_bytes[index + 2]) : 0;
			text += kAlphabet[first >> 2];
			text += kAlphabet[((first & 0x03) << 4) | (second >> 4)];
			text += index + 1 < a_bytes.size() ? kAlphabet[((second & 0x0f) << 2) | (third >> 6)] : '=';
			text += index + 2 < a_bytes.size() ? kAlphabet[third & 0x3f] : '=';
		}
		return text;
	}

	std::optional<std::string> DecodeBase64(std::string_view a_text, std::size_t a_maxBytes)
	{
		if (a_text.size() % 4 != 0 || a_text.empty()) {
			return std::nullopt;
		}
		const auto estimated = (a_text.size() / 4) * 3;
		if (estimated > a_maxBytes + 2) {
			return std::nullopt;
		}
		std::string bytes;
		bytes.reserve(estimated);
		for (std::size_t index = 0; index < a_text.size(); index += 4) {
			int values[4]{};
			std::size_t padding = 0;
			for (std::size_t offset = 0; offset < 4; ++offset) {
				const auto character = a_text[index + offset];
				if (character == '=') {
					if (offset < 2) {
						return std::nullopt;
					}
					++padding;
					values[offset] = 0;
					continue;
				}
				if (padding != 0) {
					return std::nullopt;
				}
				values[offset] = DecodeCharacter(character);
				if (values[offset] < 0) {
					return std::nullopt;
				}
			}
			bytes += static_cast<char>((values[0] << 2) | (values[1] >> 4));
			if (padding < 2) {
				bytes += static_cast<char>(((values[1] & 0x0f) << 4) | (values[2] >> 2));
			}
			if (padding < 1) {
				bytes += static_cast<char>(((values[2] & 0x03) << 6) | values[3]);
			}
			if (bytes.size() > a_maxBytes) {
				return std::nullopt;
			}
		}
		return bytes;
	}

	std::optional<Description> Describe(std::string_view a_manifest)
	{
		try {
			if (a_manifest.empty() || a_manifest.size() > kMaximumTotalBytes) {
				return std::nullopt;
			}
			const auto totalBytes = static_cast<std::uint64_t>(a_manifest.size());
			const auto chunkCount = static_cast<std::uint32_t>((a_manifest.size() + kChunkPayloadBytes - 1) / kChunkPayloadBytes);
			if (chunkCount == 0 || chunkCount > kMaximumChunks) {
				return std::nullopt;
			}
			return Description{ totalBytes, chunkCount, F4T::ContentManifest::Sha256Hex(a_manifest) };
		} catch (...) {
			return std::nullopt;
		}
	}

	std::optional<std::vector<Chunk>> Split(std::string_view a_manifest)
	{
		const auto description = Describe(a_manifest);
		if (!description) {
			return std::nullopt;
		}
		try {
			std::vector<Chunk> chunks;
			chunks.reserve(description->chunkCount);
			for (std::uint32_t index = 0; index < description->chunkCount; ++index) {
				const auto offset = static_cast<std::size_t>(index) * kChunkPayloadBytes;
				const auto length = std::min(kChunkPayloadBytes, a_manifest.size() - offset);
				chunks.push_back(Chunk{ index, EncodeBase64(a_manifest.substr(offset, length)) });
			}
			return chunks;
		} catch (...) {
			return std::nullopt;
		}
	}

	Reassembler::Reassembler(Description a_expected) :
		expected(std::move(a_expected))
	{
		if (expected.totalBytes == 0 || expected.totalBytes > kMaximumTotalBytes ||
			expected.chunkCount == 0 || expected.chunkCount > kMaximumChunks ||
			expected.chunkCount != (expected.totalBytes + kChunkPayloadBytes - 1) / kChunkPayloadBytes) {
			return;
		}
		pieces.resize(expected.chunkCount);
	}

	AddResult Reassembler::Add(std::uint32_t a_index, std::string_view a_payloadBase64)
	{
		if (pieces.empty() || expected.chunkCount == 0 || a_index >= expected.chunkCount) {
			return AddResult::invalid;
		}
		if (pieces[a_index].has_value()) {
			return AddResult::duplicate;
		}
		if (a_index != received) {
			return AddResult::outOfOrder;
		}
		const auto remaining = expected.totalBytes - static_cast<std::uint64_t>(a_index) * kChunkPayloadBytes;
		const auto expectedSize = std::min<std::uint64_t>(kChunkPayloadBytes, remaining);
		const auto decoded = DecodeBase64(a_payloadBase64, static_cast<std::size_t>(expected.totalBytes));
		if (!decoded) {
			return AddResult::invalid;
		}
		if (decoded->size() != expectedSize) {
			return AddResult::oversize;
		}
		pieces[a_index] = *decoded;
		++received;
		return AddResult::accepted;
	}

	bool Reassembler::Complete() const noexcept
	{
		return !pieces.empty() && received == pieces.size();
	}

	std::optional<std::string> Reassembler::Assemble() const
	{
		try {
			if (!Complete()) {
				return std::nullopt;
			}
			std::string manifest;
			manifest.reserve(static_cast<std::size_t>(expected.totalBytes));
			for (const auto& piece : pieces) {
				manifest += *piece;
			}
			if (manifest.size() != expected.totalBytes || F4T::ContentManifest::Sha256Hex(manifest) != expected.sha256) {
				return std::nullopt;
			}
			return manifest;
		} catch (...) {
			return std::nullopt;
		}
	}

	const Description& Reassembler::Expected() const noexcept
	{
		return expected;
	}
}
