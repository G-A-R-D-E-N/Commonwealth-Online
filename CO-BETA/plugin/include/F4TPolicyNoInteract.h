#pragma once

#include "F4TWorldPolicy.h"

#include <vector>

namespace F4T::PolicyNoInteract
{
	void Apply(const std::vector<F4T::WorldPolicy::FormKey>& a_keys);
	void Clear();
	bool Active();
}
