#include "ServerArguments.h"

#include "F4TRemotePlayerCapacity.h"

#include <cassert>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <optional>
#include <stdexcept>
#include <string>

using namespace CommonwealthOnline::Server::Arguments;

namespace
{
	void TestParsePort()
	{
		std::uint16_t port = 0;
		assert(ParsePort("1", port) && port == 1);
		assert(ParsePort("65535", port) && port == 65535);
		assert(!ParsePort("0", port));
		assert(!ParsePort("65536", port));
		assert(!ParsePort("", port));
		assert(!ParsePort("12x", port));
		assert(!ParsePort(" 5", port));
		assert(!ParsePort("-1", port));
	}

	void TestParseMaximumPlayers()
	{
		std::size_t players = 0;
		assert(ParseMaximumPlayers("1", players) && players == 1);
		assert(ParseMaximumPlayers(std::to_string(F4T::RemotePlayerCapacity::MaximumSessionPlayers), players));
		assert(players == F4T::RemotePlayerCapacity::MaximumSessionPlayers);
		assert(!ParseMaximumPlayers("0", players));
		assert(!ParseMaximumPlayers(std::to_string(F4T::RemotePlayerCapacity::MaximumSessionPlayers + 1), players));
	}

	void TestParseResumeLifetimeHours()
	{
		std::uint32_t hours = 0;
		assert(ParseResumeLifetimeHours("1", hours) && hours == 1);
		assert(ParseResumeLifetimeHours("8760", hours) && hours == 8760);
		assert(!ParseResumeLifetimeHours("0", hours));
		assert(!ParseResumeLifetimeHours("8761", hours));
	}

	void TestParseResumeLifetimeSeconds()
	{
		std::optional<std::uint32_t> seconds;
		assert(ParseResumeLifetimeSeconds("1", seconds) && seconds == 1u);
		assert(ParseResumeLifetimeSeconds(std::to_string(8760u * 3600u), seconds) && seconds == 8760u * 3600u);
		assert(!ParseResumeLifetimeSeconds("0", seconds));
		assert(!ParseResumeLifetimeSeconds(std::to_string(8760u * 3600u + 1u), seconds));
	}

	bool ThrowsRuntimeError(const std::filesystem::path& a_path)
	{
		try {
			(void)LoadConfiguration(a_path);
		} catch (const std::runtime_error&) {
			return true;
		}
		return false;
	}

	void TestLoadConfigurationRejectsMissingAndMalformedFiles()
	{
		const auto missing = std::filesystem::temp_directory_path() / "co-server-arguments-missing.json";
		std::filesystem::remove(missing);
		assert(ThrowsRuntimeError(missing));

		const auto malformed = std::filesystem::temp_directory_path() / "co-server-arguments-malformed.json";
		{
			std::ofstream output(malformed, std::ios::binary);
			output << "{";
		}
		const bool threw = ThrowsRuntimeError(malformed);
		std::filesystem::remove(malformed);
		assert(threw);
	}
}

int main()
{
	TestParsePort();
	TestParseMaximumPlayers();
	TestParseResumeLifetimeHours();
	TestParseResumeLifetimeSeconds();
	TestLoadConfigurationRejectsMissingAndMalformedFiles();
	return 0;
}
