#include "F4TMessageFragments.h"
#include "F4TTransportLimits.h"

#include <cassert>
#include <cstdint>
#include <limits>
#include <vector>

int main()
{
	using namespace F4T::MessageFragments;

	{
		std::vector<std::uint8_t> payload(1000, static_cast<std::uint8_t>('x'));
		assert(!NeedsSplit(payload, 2000));
		assert(NeedsSplit(payload, 100));
		const auto parts = Split(7, payload, 100);
		assert(parts.size() > 1);
		Assembler assembler;
		std::optional<std::vector<std::uint8_t>> assembled;
		for (const auto& part : parts) {
			assert(IsFragment(part));
			assembled = assembler.Push(part, 1.0);
		}
		assert(assembled);
		assert(*assembled == payload);
		assert(assembler.InFlightCount() == 0);
		assert(assembler.BufferedBytes() == 0);
	}

	{
		std::vector<std::uint8_t> payload(300, static_cast<std::uint8_t>('y'));
		const auto parts = Split(9, payload, 120);
		assert(parts.size() >= 2);
		Assembler assembler;
		assert(!assembler.Push(parts[0], 1.0));
		assert(assembler.InFlightCount() == 1);
		assembler.Sweep(1.0 + AssemblyTimeoutSeconds + 0.1);
		assert(assembler.InFlightCount() == 0);
		assert(assembler.BufferedBytes() == 0);
	}

	{
		std::vector<std::uint8_t> payload(200, static_cast<std::uint8_t>('z'));
		const auto parts = Split(3, payload, 80);
		Assembler assembler;
		assert(!assembler.Push(parts[0], 1.0));
		assert(!assembler.Push(parts[0], 1.1));
		for (std::size_t index = 1; index < parts.size(); ++index) {
			const auto done = assembler.Push(parts[index], 1.2 + static_cast<double>(index));
			if (index + 1 == parts.size()) {
				assert(done);
				assert(*done == payload);
			} else {
				assert(!done);
			}
		}
	}

	{
		const std::vector<std::uint8_t> payload(300, 'd');
		const auto parts = Split(103, payload, 100);
		Assembler duplicateOnly;
		assert(!duplicateOnly.Push(parts[0], 1.0));
		assert(!duplicateOnly.Push(parts[0], 9.0));
		duplicateOnly.Sweep(12.0);
		assert(duplicateOnly.InFlightCount() == 0);
		assert(duplicateOnly.BufferedBytes() == 0);

		Assembler progressing;
		assert(!progressing.Push(parts[0], 1.0));
		assert(!progressing.Push(parts[1], 9.0));
		progressing.Sweep(12.0);
		assert(progressing.InFlightCount() == 1);
		assert(progressing.BufferedBytes() > 0);
	}

	{
		std::vector<std::uint8_t> payload(200, static_cast<std::uint8_t>('h'));
		const auto parts = Split(4, payload, 80);
		Assembler assembler;
		auto headerOnly = parts[0];
		headerOnly.resize(HeaderBytes);
		assert(!assembler.Push(headerOnly, 1.0));
		assert(assembler.InFlightCount() == 0);
	}

	{
		const std::vector<std::uint8_t> overcounted{ Magic, 0x01, 0x05, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0x01, 0x00, 0x00, 0x00, 'x' };
		assert(IsFragment(overcounted));
		assert(!ReadHeader(overcounted));
		Assembler assembler;
		assert(!assembler.Push(overcounted, 1.0));
		assert(assembler.InFlightCount() == 0);
	}

	{
		const std::vector<std::uint8_t> atLimit(MaximumFragmentsPerMessage, 'x');
		const auto parts = Split(6, atLimit, HeaderBytes + 1);
		assert(parts.size() == MaximumFragmentsPerMessage);
		Assembler assembler;
		std::optional<std::vector<std::uint8_t>> result;
		for (const auto& part : parts) result = assembler.Push(part, 1.0);
		assert(result && *result == atLimit);
		const std::vector<std::uint8_t> overLimit(MaximumFragmentsPerMessage + 1, 'x');
		assert(Split(6, overLimit, HeaderBytes + 1).empty());
		auto malicious = parts.front();
		malicious[8] = 1;
		malicious[9] = 1;
		malicious[10] = 1;
		malicious[11] = 1;
		assert(!ReadHeader(malicious));
		assert(!assembler.Push(malicious, 2.0));
		assert(assembler.InFlightCount() == 0);
	}

	{
		const std::vector<std::uint8_t> payload(10, 'x');
		const auto parts = Split(8, payload, HeaderBytes + 4);
		assert(parts.size() == 3);
		Assembler assembler;
		assert(!assembler.Push(parts[0], 1.0));
		auto oversized = parts[1];
		oversized.insert(oversized.end(), 3, 'x');
		assert(!assembler.Push(oversized, 1.1));
		assert(assembler.InFlightCount() == 0);
		assert(assembler.BufferedBytes() == 0);
	}

	{
		const std::vector<std::uint8_t> payload(6, 'x');
		auto parts = Split(10, payload, HeaderBytes + 3);
		assert(parts.size() == 2);
		for (auto& part : parts) {
			part[10] = 0;
			part[11] = 0;
			part[12] = 0x40;
			part[13] = 0;
		}
		Assembler assembler;
		assert(!assembler.Push(parts[0], 1.0));
		assert(!assembler.Push(parts[1], 1.1));
		assert(assembler.InFlightCount() == 0);
		assert(assembler.BufferedBytes() == 0);
	}

	{
		const std::vector<std::uint8_t> payload(MaximumAssembledBytes, 'x');
		for (const auto frameLimit : { F4T::TransportLimits::MinimumMaximumMessageBytes,
			F4T::TransportLimits::DefaultMaximumMessageBytes }) {
			const auto parts = Split(11, payload, frameLimit);
			assert(parts.size() > 1 && parts.size() <= MaximumFragmentsPerMessage);
			Assembler assembler;
			std::optional<std::vector<std::uint8_t>> result;
			for (const auto& part : parts) {
				assert(part.size() <= frameLimit);
				result = assembler.Push(part, 1.0);
			}
			assert(result && *result == payload);
		}
		assert(Split(12, payload, (std::numeric_limits<std::size_t>::max)()).size() == 1);
	}


	{
		Assembler assembler;
		const std::vector<std::uint8_t> payload(64, 'x');
		for (std::uint32_t id = 0; id < MaximumInFlightMessages + 1; ++id) {
			const auto parts = Split(id, payload, HeaderBytes + 16);
			assert(!assembler.Push(parts.front(), 1.0));
		}
		assert(assembler.InFlightCount() == MaximumInFlightMessages);
		assert(assembler.BufferedBytes() == MaximumInFlightMessages * 16);
		assembler.Sweep(1.0 + AssemblyTimeoutSeconds + 1.0);
		assert(assembler.InFlightCount() == 0);
		assert(assembler.BufferedBytes() == 0);
	}

	assert(!IsFragment(std::vector<std::uint8_t>{ '{', '}' }));
	assert(Split(1, {}, 100).empty());
	return 0;
}
