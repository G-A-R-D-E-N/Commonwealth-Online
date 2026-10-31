#pragma once

#include "F4TWorldPolicy.h"

namespace F4T::VatsPolicy
{
	void ApplyForPolicy(const F4T::WorldPolicy::Policy& a_policy);
	void Reassert();
	void Restore();
	void OnGameLoaded();
}
