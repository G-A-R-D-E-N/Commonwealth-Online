#include "F4TProxyProtection.h"

#include <cassert>
#include <cstdint>

int main()
{
	using namespace F4T::ProxyProtection;

	assert(kBlockActivationExtraFlag == 0x1);
	assert(kHideActivateTextExtraFlag == 0x8);
	assert(kProxyActivationBlockExtraFlags == 0x9);

	constexpr std::uint32_t playerId = 0x14;
	constexpr std::uint32_t proxyId = 0xFF001234;
	constexpr std::uint32_t otherProxyId = 0xFF005678;

	assert(ShouldBlockActivation(true, proxyId, playerId));
	assert(ShouldBlockActivation(true, otherProxyId, playerId));
	assert(ShouldBlockActivation(true, proxyId, 0));

	assert(!ShouldBlockActivation(false, proxyId, playerId));
	assert(!ShouldBlockActivation(true, 0, playerId));
	assert(!ShouldBlockActivation(true, playerId, playerId));
	assert(!ShouldBlockActivation(false, 0, 0));

	return 0;
}
