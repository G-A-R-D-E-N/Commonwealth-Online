#include "F4TFactionInviteOffer.h"

#include <cassert>
#include <cstdint>

int main()
{
	using namespace F4T::FactionInviteOffer;

	constexpr std::uint32_t localId = 0x14;
	constexpr std::uint32_t remoteId = 0x21;

	assert(IsActivationAim(kActivationRange, 1.0F));
	assert(IsActivationAim(10.0F, kActivationFacingCosine));
	assert(!IsActivationAim(kActivationRange + 1.0F, 1.0F));
	assert(!IsActivationAim(10.0F, kActivationFacingCosine - 0.01F));

	assert(ShouldOffer(true, localId, remoteId, false, false));
	assert(!ShouldOffer(false, localId, remoteId, false, false));
	assert(!ShouldOffer(true, localId, 0, false, false));
	assert(!ShouldOffer(true, localId, localId, false, false));
	assert(!ShouldOffer(true, localId, remoteId, true, false));
	assert(!ShouldOffer(true, localId, remoteId, false, true));

	return 0;
}
