#pragma once

#include <cstdint>

namespace RE
{
	class Actor;
}

namespace F4T::ProxyProtection
{
	inline constexpr std::uint32_t kBlockActivationExtraFlag = 0x1;
	inline constexpr std::uint32_t kHideActivateTextExtraFlag = 0x8;
	inline constexpr std::uint32_t kProxyActivationBlockExtraFlags =
		kBlockActivationExtraFlag | kHideActivateTextExtraFlag;

	[[nodiscard]] constexpr bool ShouldBlockActivation(
		bool a_isActiveProxy,
		std::uint32_t a_targetFormID,
		std::uint32_t a_localPlayerFormID) noexcept
	{
		return a_isActiveProxy && a_targetFormID != 0 && a_targetFormID != a_localPlayerFormID;
	}

	bool ApplyToProxyActor(RE::Actor& a_proxy);
}
