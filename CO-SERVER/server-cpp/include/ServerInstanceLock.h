#pragma once

#include <cstdint>
#include <filesystem>
#include <string>

namespace CommonwealthOnline::Server
{
	class ServerInstanceLock
	{
	public:
		ServerInstanceLock() = default;
		~ServerInstanceLock();
		ServerInstanceLock(const ServerInstanceLock&) = delete;
		ServerInstanceLock& operator=(const ServerInstanceLock&) = delete;

		[[nodiscard]] bool Acquire(const std::filesystem::path& a_path, std::string& a_error);

	private:
		std::intptr_t handle_{ -1 };
	};
}
