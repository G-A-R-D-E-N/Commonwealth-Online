#pragma once

#include <atomic>

namespace F4T::LocalAdmin
{
	inline std::atomic<bool> g_isAdmin{ false };

	inline void Set(bool a_isAdmin) noexcept
	{
		g_isAdmin.store(a_isAdmin, std::memory_order_release);
	}

	[[nodiscard]] inline bool Get() noexcept
	{
		return g_isAdmin.load(std::memory_order_acquire);
	}
}
