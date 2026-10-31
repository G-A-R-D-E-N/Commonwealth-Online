#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace F4T::ModpackChunks
{
	inline constexpr std::string_view kCapabilityName = "modpack-manifest-chunks-v1";
	inline constexpr std::size_t kChunkPayloadBytes = 32 * 1024;
	inline constexpr std::size_t kMaximumTotalBytes = 2 * 1024 * 1024;
	inline constexpr std::size_t kMaximumChunks = (kMaximumTotalBytes + kChunkPayloadBytes - 1) / kChunkPayloadBytes;
	inline constexpr std::string_view kChunkPacketType = "modpackManifestChunk";
	inline constexpr std::string_view kEndPacketType = "modpackManifestEnd";
	inline constexpr std::string_view kRequestField = "requestModpackManifest";

	struct Description
	{
		std::uint64_t totalBytes{};
		std::uint32_t chunkCount{};
		std::string sha256;
	};

	struct Chunk
	{
		std::uint32_t index{};
		std::string payloadBase64;
	};

	enum class AddResult
	{
		accepted,
		invalid,
		duplicate,
		outOfOrder,
		oversize
	};

	[[nodiscard]] std::string EncodeBase64(std::string_view a_bytes);
	[[nodiscard]] std::optional<std::string> DecodeBase64(std::string_view a_text, std::size_t a_maxBytes);

	[[nodiscard]] std::optional<Description> Describe(std::string_view a_manifest);
	[[nodiscard]] std::optional<std::vector<Chunk>> Split(std::string_view a_manifest);

	class Reassembler
	{
	public:
		explicit Reassembler(Description a_expected);

		[[nodiscard]] AddResult Add(std::uint32_t a_index, std::string_view a_payloadBase64);
		[[nodiscard]] bool Complete() const noexcept;
		[[nodiscard]] std::optional<std::string> Assemble() const;
		[[nodiscard]] const Description& Expected() const noexcept;

	private:
		Description expected;
		std::vector<std::optional<std::string>> pieces;
		std::size_t received{};
	};
}
