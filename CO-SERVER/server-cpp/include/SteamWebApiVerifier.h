#pragma once

#include "DirectoryRegistrar.h"
#include "WorkerExit.h"

#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <vector>

namespace CommonwealthOnline::Server
{
	inline constexpr std::string_view kSteamWebApiKeyEnvironment = "CO_STEAM_WEB_API_KEY";
	inline constexpr std::string_view kSteamWebApiKeyFileName = ".steam-web-api-key";
	inline constexpr std::string_view kSteamWebApiKeyPage = "https://steamcommunity.com/dev/apikey";

	// Steam issues Web API keys as 32 hex characters.
	[[nodiscard]] bool IsSteamWebApiKey(std::string_view a_key) noexcept;

	struct SteamWebApiKey
	{
		std::string value;
		std::string source;
	};

	[[nodiscard]] std::optional<SteamWebApiKey> LoadSteamWebApiKey(
		const std::filesystem::path& a_baseDirectory,
		std::string& a_error);

	enum class SteamKeyCheck : std::uint8_t
	{
		Accepted,
		Rejected,
		Unreachable
	};

	struct SteamKeyCheckResult
	{
		SteamKeyCheck outcome{ SteamKeyCheck::Unreachable };
		std::string detail;
	};

	[[nodiscard]] SteamKeyCheckResult CheckSteamWebApiKey(std::string_view a_key, const DirectoryHttpSender& a_sender);

	enum class SteamTicketOutcome : std::uint8_t
	{
		Verified,
		Rejected,
		Retry,
		Unavailable
	};

	struct SteamTicketVerdict
	{
		std::uint32_t connectionId{};
		SteamTicketOutcome outcome{ SteamTicketOutcome::Unavailable };
		std::string steamId;
		std::string claimedSteamId;
		std::string profileId;
		// Safe to log: never contains the key, the ticket or a Steam ID.
		std::string detail;
	};

	[[nodiscard]] SteamTicketVerdict InterpretSteamTicketResponse(const DirectoryHttpResponse& a_response);

	struct SteamTicketAction
	{
		bool disconnect{};
		std::string code;
		std::string reason;
		std::string log;
	};

	[[nodiscard]] SteamTicketAction DecideSteamTicketAction(
		const SteamTicketVerdict& a_verdict,
		const std::function<std::optional<std::string>(std::string_view)>& a_banLookup);

	class SteamWebApiVerifier
	{
	public:
		struct Options
		{
			std::size_t maximumPending{ 64 };
			std::uint32_t attempts{ 2 };
			std::chrono::milliseconds retryDelay{ 2000 };
			std::size_t maximumPendingPerSource{ 4 };
		};

		SteamWebApiVerifier(std::string a_key, DirectoryHttpSender a_sender);
		SteamWebApiVerifier(std::string a_key, DirectoryHttpSender a_sender, Options a_options);
		SteamWebApiVerifier(const SteamWebApiVerifier&) = delete;
		SteamWebApiVerifier& operator=(const SteamWebApiVerifier&) = delete;
		~SteamWebApiVerifier();

		void Stop();

		// a_sourceKey is an IPv4 address or F4T::ConnectionPolicy::Ipv6PeerKey; 0 means the source is unknown and only maximumPending applies.
		[[nodiscard]] bool Submit(std::uint32_t a_connectionId, std::string_view a_ticketHex, std::string_view a_claimedSteamId, std::string_view a_profileId, std::uint64_t a_sourceKey = 0);
		[[nodiscard]] std::vector<SteamTicketVerdict> TakeResults();

	private:
		struct Job
		{
			std::uint32_t connectionId{};
			std::string ticketHex;
			std::string claimedSteamId;
			std::string profileId;
			std::uint32_t attempt{};
			std::chrono::steady_clock::time_point notBefore;
			std::uint64_t sourceKey{};
		};

		struct State
		{
			WorkerExit exit;
			std::string key;
			DirectoryHttpSender sender;
			Options options;
			std::deque<Job> queue;
			std::size_t inFlight{};
			std::unordered_map<std::uint64_t, std::size_t> pendingBySource;
			std::vector<SteamTicketVerdict> results;
		};

		static void Run(std::shared_ptr<State> a_state);

		std::shared_ptr<State> _state;
		std::thread _thread;
	};
}
