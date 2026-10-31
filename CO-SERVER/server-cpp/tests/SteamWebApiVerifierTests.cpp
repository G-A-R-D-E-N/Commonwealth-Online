#include "AdminTokenStore.h"
#include "F4TConnectionPolicy.h"
#include "SteamWebApiVerifier.h"

#include <atomic>
#include <cassert>
#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <filesystem>
#include <future>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace
{
	using CommonwealthOnline::Server::DirectoryHttpResponse;
	using CommonwealthOnline::Server::DirectoryHttpSender;
	using CommonwealthOnline::Server::SteamKeyCheck;
	using CommonwealthOnline::Server::SteamTicketOutcome;
	using CommonwealthOnline::Server::SteamTicketVerdict;
	using CommonwealthOnline::Server::SteamWebApiVerifier;
	using namespace std::chrono_literals;

	constexpr std::string_view kKey = "0123456789ABCDEF0123456789abcdef";
	constexpr std::string_view kSteamId = "76561198000000042";
	constexpr std::string_view kOtherSteamId = "76561198000000099";

	DirectoryHttpResponse Ok(std::string_view a_steamId)
	{
		return { 200, R"({"response":{"params":{"result":"OK","steamid":")" + std::string{ a_steamId } +
			R"(","ownersteamid":")" + std::string{ a_steamId } + R"(","vacbanned":false,"publisherbanned":false}}})", {} };
	}

	DirectoryHttpResponse TicketError()
	{
		return { 200, R"({"response":{"error":{"errorcode":101,"errordesc":"Invalid ticket"}}})", {} };
	}

	DirectoryHttpResponse Forbidden()
	{
		return { 403, "<html><body>Forbidden</body></html>", {} };
	}

	void SetEnvironment(std::string_view a_value)
	{
		const std::string name{ CommonwealthOnline::Server::kSteamWebApiKeyEnvironment };
		const std::string value{ a_value };
#ifdef _WIN32
		(void)_putenv_s(name.c_str(), value.c_str());
#else
		if (value.empty()) {
			(void)unsetenv(name.c_str());
		} else {
			(void)setenv(name.c_str(), value.c_str(), 1);
		}
#endif
	}

	std::vector<SteamTicketVerdict> WaitForResults(SteamWebApiVerifier& a_verifier, std::size_t a_count)
	{
		std::vector<SteamTicketVerdict> results;
		const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{ 10 };
		while (results.size() < a_count && std::chrono::steady_clock::now() < deadline) {
			for (auto& verdict : a_verifier.TakeResults()) results.push_back(std::move(verdict));
			std::this_thread::sleep_for(std::chrono::milliseconds{ 5 });
		}
		return results;
	}

	void TestKeyFormat()
	{
		using CommonwealthOnline::Server::IsSteamWebApiKey;
		assert(IsSteamWebApiKey(kKey));
		assert(!IsSteamWebApiKey(""));
		assert(!IsSteamWebApiKey(kKey.substr(1)));
		assert(!IsSteamWebApiKey(std::string{ kKey } + "0"));
		assert(!IsSteamWebApiKey("0123456789ABCDEF0123456789ABCDEG"));
		assert(!IsSteamWebApiKey("0123456789ABCDEF0123456789ABCDE&"));
	}

	void TestInterpretResponse()
	{
		using CommonwealthOnline::Server::InterpretSteamTicketResponse;
		const auto verified = InterpretSteamTicketResponse(Ok(kSteamId));
		assert(verified.outcome == SteamTicketOutcome::Verified && verified.steamId == kSteamId);

		assert(InterpretSteamTicketResponse(TicketError()).outcome == SteamTicketOutcome::Retry);
		assert(InterpretSteamTicketResponse(Forbidden()).outcome == SteamTicketOutcome::Unavailable);
		assert(InterpretSteamTicketResponse({ 429, {}, {} }).outcome == SteamTicketOutcome::Unavailable);
		assert(InterpretSteamTicketResponse({ 500, {}, {} }).outcome == SteamTicketOutcome::Unavailable);
		assert(InterpretSteamTicketResponse({ 0, {}, "timed out" }).outcome == SteamTicketOutcome::Unavailable);
		assert(InterpretSteamTicketResponse({ 200, "not json", {} }).outcome == SteamTicketOutcome::Unavailable);
		assert(InterpretSteamTicketResponse({ 200, R"({"response":{}})", {} }).outcome == SteamTicketOutcome::Unavailable);

		const auto notOk = InterpretSteamTicketResponse({ 200, R"({"response":{"params":{"result":"Invalid","steamid":"76561198000000042"}}})", {} });
		assert(notOk.outcome == SteamTicketOutcome::Rejected && notOk.steamId.empty());
		const auto clan = InterpretSteamTicketResponse(Ok("103582791429521408"));
		assert(clan.outcome == SteamTicketOutcome::Rejected && clan.steamId.empty());

		for (const auto& response : { Ok(kSteamId), TicketError(), Forbidden() }) {
			const auto detail = InterpretSteamTicketResponse(response).detail;
			assert(detail.find(kSteamId) == std::string::npos);
		}
	}

	void TestCheckKey()
	{
		using CommonwealthOnline::Server::CheckSteamWebApiKey;
		std::vector<std::string> urls;
		auto respond = [&urls](DirectoryHttpResponse a_response) -> DirectoryHttpSender {
			return [&urls, a_response](std::string_view a_method, const std::string& a_url, const std::string&, const std::string&) {
				assert(a_method == "GET");
				urls.push_back(a_url);
				return a_response;
			};
		};
		assert(CheckSteamWebApiKey(kKey, respond(TicketError())).outcome == SteamKeyCheck::Accepted);
		assert(urls.back().find("key=" + std::string{ kKey }) != std::string::npos);
		assert(urls.back().find("appid=377160") != std::string::npos);

		const auto rejected = CheckSteamWebApiKey(kKey, respond(Forbidden()));
		assert(rejected.outcome == SteamKeyCheck::Rejected && rejected.detail.find(kKey) == std::string::npos);
		assert(CheckSteamWebApiKey(kKey, respond({ 0, {}, "no route" })).outcome == SteamKeyCheck::Unreachable);
		assert(CheckSteamWebApiKey(kKey, respond({ 503, {}, {} })).outcome == SteamKeyCheck::Unreachable);

		const auto before = urls.size();
		assert(CheckSteamWebApiKey("short", respond(TicketError())).outcome == SteamKeyCheck::Rejected);
		assert(urls.size() == before);
	}

	void TestDecideAction()
	{
		using CommonwealthOnline::Server::DecideSteamTicketAction;
		const auto banLookup = [](std::string_view a_steamId) -> std::optional<std::string> {
			if (a_steamId == kOtherSteamId) return std::string{ "cheating" };
			return std::nullopt;
		};

		SteamTicketVerdict verdict{ 7, SteamTicketOutcome::Verified, std::string{ kSteamId }, {}, {}, {} };
		auto action = DecideSteamTicketAction(verdict, banLookup);
		assert(!action.disconnect && action.log.find(kSteamId) == std::string::npos);

		verdict.claimedSteamId = kSteamId;
		assert(!DecideSteamTicketAction(verdict, banLookup).disconnect);

		verdict.claimedSteamId = kOtherSteamId;
		action = DecideSteamTicketAction(verdict, banLookup);
		assert(action.disconnect && action.code == "steam_identity_mismatch");
		assert(action.log.find(kSteamId) == std::string::npos && action.log.find(kOtherSteamId) == std::string::npos);

		verdict = { 8, SteamTicketOutcome::Verified, std::string{ kOtherSteamId }, {}, {}, {} };
		action = DecideSteamTicketAction(verdict, banLookup);
		assert(action.disconnect && action.code == "steam_banned" && action.reason == "cheating");
		assert(action.log.find(kOtherSteamId) == std::string::npos);

		for (const auto outcome : { SteamTicketOutcome::Rejected, SteamTicketOutcome::Unavailable }) {
			verdict = { 9, outcome, {}, std::string{ kOtherSteamId }, {}, "detail" };
			assert(!DecideSteamTicketAction(verdict, banLookup).disconnect);
		}
	}

	void TestVerifierDeliversVerdicts()
	{
		std::mutex mutex;
		std::vector<std::string> urls;
		SteamWebApiVerifier verifier{ std::string{ kKey }, [&](std::string_view, const std::string& a_url, const std::string&, const std::string&) {
			std::lock_guard lock{ mutex };
			urls.push_back(a_url);
			return Ok(kSteamId);
		} };
		assert(!verifier.Submit(1, "00&key=x", {}, {}));
		assert(!verifier.Submit(1, "", {}, {}));
		assert(verifier.Submit(11, "14000000ABCDEF", kSteamId, "profile-11"));
		const auto results = WaitForResults(verifier, 1);
		assert(results.size() == 1);
		assert(results[0].connectionId == 11 && results[0].outcome == SteamTicketOutcome::Verified);
		assert(results[0].steamId == kSteamId && results[0].claimedSteamId == kSteamId && results[0].profileId == "profile-11");
		std::lock_guard lock{ mutex };
		assert(urls.size() == 1 && urls[0].find("ticket=14000000ABCDEF") != std::string::npos);
	}

	void TestVerifierRetriesOnce()
	{
		std::atomic_int calls{ 0 };
		SteamWebApiVerifier verifier{
			std::string{ kKey },
			[&](std::string_view, const std::string&, const std::string&, const std::string&) {
				return calls.fetch_add(1) == 0 ? TicketError() : Ok(kSteamId);
			},
			SteamWebApiVerifier::Options{ .maximumPending = 4, .attempts = 2, .retryDelay = std::chrono::milliseconds{ 10 } } };
		assert(verifier.Submit(12, "AB", {}, {}));
		const auto results = WaitForResults(verifier, 1);
		assert(results.size() == 1 && results[0].outcome == SteamTicketOutcome::Verified && calls == 2);

		SteamWebApiVerifier failing{
			std::string{ kKey },
			[](std::string_view, const std::string&, const std::string&, const std::string&) { return TicketError(); },
			SteamWebApiVerifier::Options{ .maximumPending = 4, .attempts = 2, .retryDelay = std::chrono::milliseconds{ 10 } } };
		assert(failing.Submit(13, "AB", {}, {}));
		const auto failed = WaitForResults(failing, 1);
		assert(failed.size() == 1 && failed[0].outcome == SteamTicketOutcome::Rejected);
	}

	void TestVerifierSurvivesMalformedBodies()
	{
		const std::vector<std::string> bodies{
			R"({"response":{"params":{"result":1}}})",
			R"({"response":{"error":{"errorcode":"x"}}})"
		};
		for (const auto& body : bodies) {
			SteamWebApiVerifier verifier{
				std::string{ kKey },
				[&body](std::string_view, const std::string&, const std::string&, const std::string&) {
					return DirectoryHttpResponse{ 200, body, {} };
				},
				SteamWebApiVerifier::Options{ .maximumPending = 4, .attempts = 1, .retryDelay = std::chrono::milliseconds{ 10 } } };
			assert(verifier.Submit(31, "AB", kSteamId, "profile-31"));
			const auto results = WaitForResults(verifier, 1);
			assert(results.size() == 1);
			assert(results[0].connectionId == 31 && results[0].profileId == "profile-31");
			assert(results[0].outcome == SteamTicketOutcome::Unavailable);
			assert(results[0].steamId.empty());
			assert(results[0].detail.find(kSteamId) == std::string::npos);

			assert(verifier.Submit(32, "AB", {}, {}));
			const auto next = WaitForResults(verifier, 1);
			assert(next.size() == 1 && next[0].connectionId == 32);
		}
	}

	struct BlockingSteam : std::enable_shared_from_this<BlockingSteam>
	{
		std::mutex mutex;
		std::condition_variable changed;
		bool entered{};
		bool released{};
		int completed{};
		std::atomic_int calls{};

		DirectoryHttpSender Sender()
		{
			auto self = shared_from_this();
			return [self](std::string_view, const std::string&, const std::string&, const std::string&) {
				self->calls.fetch_add(1);
				std::unique_lock lock{ self->mutex };
				self->entered = true;
				self->changed.notify_all();
				self->changed.wait_for(lock, 10s, [self] { return self->released; });
				++self->completed;
				self->changed.notify_all();
				return TicketError();
			};
		}

		bool WaitForEntry()
		{
			std::unique_lock lock{ mutex };
			return changed.wait_for(lock, 10s, [this] { return entered; });
		}

		bool WaitForCompletion()
		{
			std::unique_lock lock{ mutex };
			return changed.wait_for(lock, 10s, [this] { return completed > 0; });
		}

		void Release()
		{
			{
				std::lock_guard lock{ mutex };
				released = true;
			}
			changed.notify_all();
		}
	};

	void TestVerifierShutdownDoesNotWaitForSteam()
	{
		auto steam = std::make_shared<BlockingSteam>();
		SteamWebApiVerifier verifier{
			std::string{ kKey },
			steam->Sender(),
			SteamWebApiVerifier::Options{ .maximumPending = 4, .attempts = 2, .retryDelay = std::chrono::milliseconds{ 10 } } };
		assert(verifier.Submit(41, "AB", kSteamId, "profile-41"));
		assert(steam->WaitForEntry());

		const auto started = std::chrono::steady_clock::now();
		verifier.Stop();
		assert(std::chrono::steady_clock::now() - started < 2s);
		assert(steam->calls.load() == 1);
		assert(!verifier.Submit(42, "AB", kSteamId, "profile-42"));

		steam->Release();
		assert(steam->WaitForCompletion());
		std::this_thread::sleep_for(200ms);
		assert(steam->calls.load() == 1);
		assert(verifier.TakeResults().empty());
	}

	void TestVerifierQueueIsBounded()
	{
		std::promise<void> release;
		auto released = release.get_future().share();
		std::atomic_int started{ 0 };
		{
			SteamWebApiVerifier verifier{
				std::string{ kKey },
				[&, released](std::string_view, const std::string&, const std::string&, const std::string&) {
					started.fetch_add(1);
					released.wait();
					return Ok(kSteamId);
				},
				SteamWebApiVerifier::Options{ .maximumPending = 2, .attempts = 1, .retryDelay = std::chrono::milliseconds{ 10 } } };
			assert(verifier.Submit(21, "AB", {}, {}));
			while (started.load() == 0) std::this_thread::yield();
			assert(verifier.Submit(22, "AB", {}, {}));
			assert(!verifier.Submit(23, "AB", {}, {}));
			release.set_value();
			const auto results = WaitForResults(verifier, 2);
			assert(results.size() == 2);
		}
	}

	void TestVerifierCapsPendingPerSource()
	{
		std::promise<void> release;
		auto released = release.get_future().share();
		std::atomic_int started{ 0 };
		constexpr std::uint32_t kFlooder = 0x0A000001;
		constexpr std::uint32_t kPlayer = 0x0A000002;
		{
			SteamWebApiVerifier verifier{
				std::string{ kKey },
				[&, released](std::string_view, const std::string&, const std::string&, const std::string&) {
					started.fetch_add(1);
					released.wait();
					return TicketError();
				},
				SteamWebApiVerifier::Options{
					.maximumPending = 8, .attempts = 2, .retryDelay = std::chrono::milliseconds{ 10 }, .maximumPendingPerSource = 2 } };
			assert(verifier.Submit(51, "AB", {}, {}, kFlooder));
			while (started.load() == 0) std::this_thread::yield();
			assert(verifier.Submit(52, "AB", {}, {}, kFlooder));
			assert(!verifier.Submit(53, "AB", {}, {}, kFlooder));
			assert(verifier.Submit(54, "AB", {}, {}, kPlayer));
			assert(verifier.Submit(55, "AB", {}, {}));
			assert(verifier.Submit(56, "AB", {}, {}));
			release.set_value();
			const auto results = WaitForResults(verifier, 5);
			assert(results.size() == 5);
			for (const auto& result : results) {
				assert(result.outcome == SteamTicketOutcome::Rejected);
			}
			assert(verifier.Submit(57, "AB", {}, {}, kFlooder));
			assert(verifier.Submit(58, "AB", {}, {}, kFlooder));
			assert(WaitForResults(verifier, 2).size() == 2);
		}
	}

	void TestVerifierCapsPendingPerIpv6Prefix()
	{
		using F4T::ConnectionPolicy::Ipv6PeerKey;
		using F4T::ConnectionPolicy::Ipv6Prefix64;
		std::promise<void> release;
		auto released = release.get_future().share();
		std::atomic_int started{ 0 };
		const auto flooder = Ipv6PeerKey(*Ipv6Prefix64("2606:4700::1"));
		const auto sameHost = Ipv6PeerKey(*Ipv6Prefix64("2606:4700:0:0:ffff::2"));
		const auto otherHost = Ipv6PeerKey(*Ipv6Prefix64("2606:4701::1"));
		assert(flooder == sameHost && flooder != otherHost);
		{
			SteamWebApiVerifier verifier{
				std::string{ kKey },
				[&, released](std::string_view, const std::string&, const std::string&, const std::string&) {
					started.fetch_add(1);
					released.wait();
					return TicketError();
				},
				SteamWebApiVerifier::Options{
					.maximumPending = 8, .attempts = 2, .retryDelay = std::chrono::milliseconds{ 10 }, .maximumPendingPerSource = 2 } };
			assert(verifier.Submit(61, "AB", {}, {}, flooder));
			while (started.load() == 0) std::this_thread::yield();
			assert(verifier.Submit(62, "AB", {}, {}, sameHost));
			assert(!verifier.Submit(63, "AB", {}, {}, flooder));
			assert(verifier.Submit(64, "AB", {}, {}, otherHost));
			assert(verifier.Submit(65, "AB", {}, {}, 0x0A000001));
			release.set_value();
			assert(WaitForResults(verifier, 4).size() == 4);
			assert(verifier.Submit(66, "AB", {}, {}, flooder));
			assert(verifier.Submit(67, "AB", {}, {}, flooder));
			assert(!verifier.Submit(68, "AB", {}, {}, flooder));
			assert(WaitForResults(verifier, 2).size() == 2);
		}
	}

	void TestLoadKey()
	{
		using CommonwealthOnline::Server::LoadSteamWebApiKey;
		const auto directory = std::filesystem::temp_directory_path() /
			("co-steam-key-test-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
		std::filesystem::create_directories(directory);
		const auto keyFile = directory / CommonwealthOnline::Server::kSteamWebApiKeyFileName;
		std::string error;

		SetEnvironment("");
		assert(!LoadSteamWebApiKey(directory, error) && error.empty());

		CommonwealthOnline::Server::AdminTokenStore::Replace(keyFile, std::string{ kKey });
		auto loaded = LoadSteamWebApiKey(directory, error);
		assert(loaded && loaded->value == kKey && loaded->source == keyFile.string());

		const std::string environmentKey = "FEDCBA9876543210FEDCBA9876543210";
		SetEnvironment(environmentKey);
		loaded = LoadSteamWebApiKey(directory, error);
		assert(loaded && loaded->value == environmentKey && loaded->source == CommonwealthOnline::Server::kSteamWebApiKeyEnvironment);

		SetEnvironment("not-a-key");
		assert(!LoadSteamWebApiKey(directory, error) && !error.empty() && error.find("not-a-key") == std::string::npos);

		SetEnvironment("");
		CommonwealthOnline::Server::AdminTokenStore::Replace(keyFile, "too-short");
		assert(!LoadSteamWebApiKey(directory, error) && !error.empty() && error.find("too-short") == std::string::npos);

		std::error_code ignored;
		std::filesystem::remove_all(directory, ignored);
	}
}

int main()
{
	TestKeyFormat();
	TestInterpretResponse();
	TestCheckKey();
	TestDecideAction();
	TestVerifierDeliversVerdicts();
	TestVerifierRetriesOnce();
	TestVerifierSurvivesMalformedBodies();
	TestVerifierShutdownDoesNotWaitForSteam();
	TestVerifierQueueIsBounded();
	TestVerifierCapsPendingPerSource();
	TestVerifierCapsPendingPerIpv6Prefix();
	TestLoadKey();
	return 0;
}
