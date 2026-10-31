#include "F4TProbeVersionGate.h"

#include <cassert>

namespace
{
	using F4T::ProbeVersionGate::IsSupported;
	using F4T::ProbeVersionGate::Runtime;
	using F4T::ProbeVersionGate::RuntimeOf;

	void ExpectSupported(std::uint16_t a_major, std::uint16_t a_minor, std::uint16_t a_patch)
	{
		assert(IsSupported({ a_major, a_minor, a_patch }));
	}

	void ExpectRejected(std::uint16_t a_major, std::uint16_t a_minor, std::uint16_t a_patch)
	{
		assert(!IsSupported({ a_major, a_minor, a_patch }));
	}
}

int main()
{
	ExpectSupported(1, 10, 163);
	ExpectSupported(1, 10, 984);
	ExpectSupported(1, 11, 240);

	ExpectRejected(1, 10, 162);
	ExpectRejected(1, 10, 164);
	ExpectRejected(1, 10, 985);
	ExpectRejected(1, 11, 221);
	ExpectRejected(1, 11, 239);
	ExpectRejected(1, 11, 241);
	ExpectRejected(1, 9, 4);
	ExpectRejected(0, 0, 0);
	ExpectRejected(2, 0, 0);
	assert(!IsSupported({ 1, 10, 163, 1 }));
	assert(!IsSupported({ 1, 10, 984, 1 }));
	assert(!IsSupported({ 1, 11, 240, 1 }));
	assert(RuntimeOf({ 1, 10, 163 }) == Runtime::Og);
	assert(RuntimeOf({ 1, 10, 984 }) == Runtime::Ng);
	assert(RuntimeOf({ 1, 11, 240 }) == Runtime::Ae);
	assert(!RuntimeOf({ 1, 10, 162 }));
	return 0;
}
