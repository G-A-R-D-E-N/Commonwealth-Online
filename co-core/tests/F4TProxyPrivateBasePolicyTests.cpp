#include "F4TProxyPrivateBasePolicy.h"

#include <cassert>
#include <cstddef>

int main()
{
	using namespace F4T::ProxyPrivateBasePolicy;

	static_assert(kMaxPrivateBasesPerSex == 8);
	assert(MayBuildPrivateBase(0));
	assert(MayBuildPrivateBase(kMaxPrivateBasesPerSex - 1));
	assert(!MayBuildPrivateBase(kMaxPrivateBasesPerSex));
	assert(!MayBuildPrivateBase(kMaxPrivateBasesPerSex + 5));

	assert(MayParkPrivateBase(0));
	assert(MayParkPrivateBase(kMaxPrivateBasesPerSex - 1));
	assert(!MayParkPrivateBase(kMaxPrivateBasesPerSex));

	std::size_t live = 0;
	std::size_t parked = 0;
	for (std::size_t step = 0; step < 100; ++step) {
		if (MayBuildPrivateBase(live)) {
			++live;
		}
		if (step % 3 == 0 && live > 0) {
			--live;
			if (MayParkPrivateBase(parked)) {
				++parked;
			}
		}
		assert(live <= kMaxPrivateBasesPerSex);
		assert(parked <= kMaxPrivateBasesPerSex);
	}
	return 0;
}
