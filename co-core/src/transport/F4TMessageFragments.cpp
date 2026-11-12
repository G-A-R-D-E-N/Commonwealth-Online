#include "F4TMessageFragments.h"

#include <algorithm>
#include <cstring>

namespace F4T::MessageFragments
{
	namespace
	{
		void WriteU16(std::uint8_t* a_out, std::uint16_t a_value) noexcept
		{
			a_out[0] = static_cast<std::uint8_t>(a_value & 0xFF);
			a_out[1] = static_cast<std::uint8_t>((a_value >> 8) & 0xFF);
		}

		void WriteU32(std::uint8_t* a_out, std::uint32_t a_value) noexcept
		{
			a_out[0] = static_cast<std::uint8_t>(a_value & 0xFF);
			a_out[1] = static_cast<std::uint8_t>((a_value >> 8) & 0xFF);
			a_out[2] = static_cast<std::uint8_t>((a_value >> 16) & 0xFF);
			a_out[3] = static_cast<std::uint8_t>((a_value >> 24) & 0xFF);
		}

		std::uint16_t ReadU16(const std::uint8_t* a_in) noexcept
		{
			return static_cast<std::uint16_t>(a_in[0] | (static_cast<std::uint16_t>(a_in[1]) << 8));
		}

		std::uint32_t ReadU32(const std::uint8_t* a_in) noexcept
		{
			return static_cast<std::uint32_t>(a_in[0]) |
				(static_cast<std::uint32_t>(a_in[1]) << 8) |
				(static_cast<std::uint32_t>(a_in[2]) << 16) |
				(static_cast<std::uint32_t>(a_in[3]) << 24);
		}
	}

	bool IsFragment(std::span<const std::uint8_t> a_payload) noexcept
	{
		return a_payload.size() >= HeaderBytes && a_payload[0] == Magic && a_payload[1] == 0x01;
	}

	std::optional<Header> ReadHeader(std::span<const std::uint8_t> a_payload) noexcept
	{
		if (!IsFragment(a_payload)) {
			return std::nullopt;
		}
		Header header{};
		header.messageId = ReadU32(a_payload.data() + 2);
		header.fragmentIndex = ReadU16(a_payload.data() + 6);
		header.fragmentCount = ReadU16(a_payload.data() + 8);
		header.totalSize = ReadU32(a_payload.data() + 10);
		if (header.fragmentCount == 0 || header.fragmentIndex >= header.fragmentCount ||
			header.fragmentCount > MaximumFragmentsPerMessage ||
			header.fragmentCount > header.totalSize || header.totalSize == 0 ||
			header.totalSize > MaximumAssembledBytes) {
			return std::nullopt;
		}
		return header;
	}

	bool NeedsSplit(std::span<const std::uint8_t> a_payload, std::size_t a_maximumFragmentBytes) noexcept
	{
		return a_maximumFragmentBytes > HeaderBytes && a_payload.size() > a_maximumFragmentBytes;
	}

	std::vector<std::vector<std::uint8_t>> Split(
		std::uint32_t a_messageId,
		std::span<const std::uint8_t> a_payload,
		std::size_t a_maximumFragmentBytes)
	{
		std::vector<std::vector<std::uint8_t>> out;
		if (a_payload.empty() || a_payload.size() > MaximumAssembledBytes ||
			a_maximumFragmentBytes <= HeaderBytes) {
			return out;
		}

		const auto bodyCapacity = a_maximumFragmentBytes - HeaderBytes;
		const auto count = 1 + (a_payload.size() - 1) / bodyCapacity;
		if (count == 0 || count > MaximumFragmentsPerMessage) {
			return out;
		}

		out.reserve(count);
		std::size_t offset = 0;
		for (std::uint16_t index = 0; index < static_cast<std::uint16_t>(count); ++index) {
			const auto take = (std::min)(bodyCapacity, a_payload.size() - offset);
			std::vector<std::uint8_t> fragment(HeaderBytes + take);
			fragment[0] = Magic;
			fragment[1] = 0x01;
			WriteU32(fragment.data() + 2, a_messageId);
			WriteU16(fragment.data() + 6, index);
			WriteU16(fragment.data() + 8, static_cast<std::uint16_t>(count));
			WriteU32(fragment.data() + 10, static_cast<std::uint32_t>(a_payload.size()));
			std::memcpy(fragment.data() + HeaderBytes, a_payload.data() + offset, take);
			offset += take;
			out.push_back(std::move(fragment));
		}
		return out;
	}

	std::optional<std::vector<std::uint8_t>> Assembler::Push(
		std::span<const std::uint8_t> a_fragment,
		double a_nowSeconds)
	{
		Sweep(a_nowSeconds);
		const auto header = ReadHeader(a_fragment);
		if (!header) {
			return std::nullopt;
		}
		const auto body = a_fragment.subspan(HeaderBytes);
		if (body.empty()) {
			return std::nullopt;
		}

		auto found = _pending.find(header->messageId);
		if (found == _pending.end()) {
			if (_pending.size() >= MaximumInFlightMessages) {
				const auto oldest = std::min_element(_pending.begin(), _pending.end(), [](const auto& a_left, const auto& a_right) {
					return a_left.second.updatedAt < a_right.second.updatedAt;
				});
				_bufferedBytes -= oldest->second.buffered;
				_pending.erase(oldest);
			}
			Pending pending{};
			pending.header = *header;
			pending.parts.assign(header->fragmentCount, {});
			pending.updatedAt = a_nowSeconds;
			found = _pending.emplace(header->messageId, std::move(pending)).first;
		}

		auto& pending = found->second;
		if (pending.header.fragmentCount != header->fragmentCount ||
			pending.header.totalSize != header->totalSize) {
			_bufferedBytes -= pending.buffered;
			_pending.erase(found);
			return std::nullopt;
		}
		if (!pending.parts[header->fragmentIndex].empty()) {
			return std::nullopt;
		}
		if (body.size() > header->totalSize - pending.buffered ||
			body.size() > MaximumAssembledBytes - _bufferedBytes) {
			_bufferedBytes -= pending.buffered;
			_pending.erase(found);
			return std::nullopt;
		}

		pending.parts[header->fragmentIndex].assign(body.begin(), body.end());
		pending.buffered += body.size();
		_bufferedBytes += body.size();
		pending.received += 1;
		pending.updatedAt = a_nowSeconds;

		if (pending.received < pending.header.fragmentCount) {
			return std::nullopt;
		}

		const auto expectedSize = pending.header.totalSize;
		const auto buffered = pending.buffered;
		if (buffered != expectedSize) {
			_bufferedBytes -= buffered;
			_pending.erase(found);
			return std::nullopt;
		}
		std::vector<std::uint8_t> assembled;
		assembled.reserve(expectedSize);
		for (const auto& part : pending.parts) {
			assembled.insert(assembled.end(), part.begin(), part.end());
		}
		_bufferedBytes -= buffered;
		_pending.erase(found);
		if (assembled.size() != expectedSize || assembled.size() > MaximumAssembledBytes) {
			return std::nullopt;
		}
		return assembled;
	}

	void Assembler::Sweep(double a_nowSeconds) noexcept
	{
		for (auto it = _pending.begin(); it != _pending.end();) {
			if (a_nowSeconds - it->second.updatedAt > AssemblyTimeoutSeconds) {
				_bufferedBytes -= it->second.buffered;
				it = _pending.erase(it);
			} else {
				++it;
			}
		}
	}

	std::size_t Assembler::InFlightCount() const noexcept
	{
		return _pending.size();
	}

	std::size_t Assembler::BufferedBytes() const noexcept
	{
		return _bufferedBytes;
	}
}
