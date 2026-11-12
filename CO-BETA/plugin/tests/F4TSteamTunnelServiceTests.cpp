#include "F4TSteamTunnelServiceTestSupport.h"

namespace F4T::DebugLogging
{
	bool IsEnabled()
	{
		return true;
	}

	void Write(std::string_view a_component, std::string_view a_message)
	{
		std::lock_guard lock{ RecordedLog::g_mutex };
		RecordedLog::g_lines.push_back(std::string{ a_component } + ": " + std::string{ a_message });
	}
}

namespace
{
	void RunChecks()
	{
		ServiceTest::RunCoreChecks();
		ServiceTest::RunPresenceChecks();
		ServiceTest::RunLobbyChecks();
		ServiceTest::RunGuestChecks();
	}
}

int main()
{
	F4T::Steam::SetIdentityBinderFactory({});
	std::atomic<bool> finished{ false };
	std::thread checks([&finished] {
		RunChecks();
		finished.store(true);
	});

	const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{ 30 };
	while (!finished.load() && std::chrono::steady_clock::now() < deadline) {
		std::this_thread::sleep_for(std::chrono::milliseconds{ 10 });
	}
	if (!finished.load()) {
		std::fputs("F4TSteamTunnelServiceTests timed out\n", stderr);
		std::_Exit(1);
	}

	checks.join();
	return 0;
}
