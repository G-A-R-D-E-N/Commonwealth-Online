#pragma once

#include <functional>

namespace F4T::NpcSync
{
	void Update(const std::function<void(const std::function<void()>&)>& a_runGameplay);

	void Reset();
}
