#pragma once

#include <cstdint>
#include <functional>

namespace F4T::SurvivalEnforcer
{
	bool Tick(std::uint64_t a_connection, std::uint64_t a_load, bool a_loaded, const std::function<void()>& a_work);
	void NotifyUnavailable(std::uint64_t a_connection);
	void ClearNotice();
}
