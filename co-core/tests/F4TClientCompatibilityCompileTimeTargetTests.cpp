#include "F4TClientCompatibility.h"

#include <cassert>

namespace
{
	void TestTargetSelected()
	{
		assert(F4T::ClientCompatibility::kBuildTargetSelected);
		const auto target = F4T::ClientCompatibility::ResolveCompileTimeTarget();
		assert(target.has_value());
#if defined(F4T_TARGET_AE) || defined(F4T_TARGET_CURRENT)
		constexpr auto expected = F4T::ClientCompatibility::BuildTarget::AE;
		constexpr auto expectedRuntime = F4T::ClientCompatibility::kAeRuntimeVersion;
#elif defined(F4T_TARGET_NG)
		constexpr auto expected = F4T::ClientCompatibility::BuildTarget::NG;
		constexpr auto expectedRuntime = F4T::ClientCompatibility::kNgRuntimeVersion;
#elif defined(F4T_TARGET_OG)
		constexpr auto expected = F4T::ClientCompatibility::BuildTarget::OG;
		constexpr auto expectedRuntime = F4T::ClientCompatibility::kOgRuntimeVersion;
#else
#error "A client target define is required"
#endif
		assert(*target == expected);
		assert(F4T::ClientCompatibility::Find(expected)->falloutRuntimeVersion == expectedRuntime);
	}
}

int main()
{
	TestTargetSelected();
	return 0;
}
