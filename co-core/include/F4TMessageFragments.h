#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <unordered_map>
#include <vector>

namespace F4T::MessageFragments
{
	inline constexpr std::uint8_t Magic = 0xF4;
	inline constexpr std::size_t HeaderBytes = 14;
	inline constexpr std::size_t MaximumAssembledBytes = 4uz * 1024uz * 1024uz;
	inline constexpr std::size_t MaximumFragmentsPerMessage = 256;
	inline constexpr std::size_t MaximumInFlightMessages = 8;
	inline constexpr double AssemblyTimeoutSeconds = 10.0;

	struct Header
	{
		std::uint32_t messageId{};
		std::uint16_t fragmentIndex{};
		std::uint16_t fragmentCount{};
		std::uint32_t totalSize{};
	};

	[[nodiscard]] bool IsFragment(std::span<const std::uint8_t> a_payload) noexcept;
	[[nodiscard]] std::optional<Header> ReadHeader(std::span<const std::uint8_t> a_payload) noexcept;
	[[nodiscard]] std::vector<std::vector<std::uint8_t>> Split(
		std::uint32_t a_messageId,
		std::span<const std::uint8_t> a_payload,
		std::size_t a_maximumFragmentBytes);
	[[nodiscard]] bool NeedsSplit(std::span<const std::uint8_t> a_payload, std::size_t a_maximumFragmentBytes) noexcept;

	class Assembler
	{
	public:
		[[nodiscard]] std::optional<std::vector<std::uint8_t>> Push(
			std::span<const std::uint8_t> a_fragment,
			double a_nowSeconds);
		void Sweep(double a_nowSeconds) noexcept;
		[[nodiscard]] std::size_t InFlightCount() const noexcept;
		[[nodiscard]] std::size_t BufferedBytes() const noexcept;

	private:
		struct Pending
		{
			Header header{};
			std::vector<std::vector<std::uint8_t>> parts;
			std::size_t received{};
			std::size_t buffered{};
			double updatedAt{};
		};

		std::unordered_map<std::uint32_t, Pending> _pending;
		std::size_t _bufferedBytes{};
	};
}
