#include "DirectoryRegistrar.h"

#include <nlohmann/json.hpp>

#include <atomic>
#include <cassert>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace
{
	using CommonwealthOnline::Server::DirectoryHttpResponse;
	using CommonwealthOnline::Server::DirectoryListing;
	using CommonwealthOnline::Server::DirectoryRegistrar;
	using CommonwealthOnline::Server::DirectoryState;
	using Json = nlohmann::json;
	using namespace std::chrono_literals;

	struct Call
	{
		std::string method;
		std::string url;
		std::string bearer;
		Json body;
	};

	struct FakeDirectory
	{
		std::deque<DirectoryHttpResponse> responses;
		std::vector<Call> calls;

		CommonwealthOnline::Server::DirectoryHttpSender Sender()
		{
			return [this](std::string_view a_method, const std::string& a_url, const std::string& a_bearer, const std::string& a_body) {
				calls.push_back(Call{ std::string{ a_method }, a_url, a_bearer, a_body.empty() ? Json{} : Json::parse(a_body) });
				assert(!responses.empty());
				auto response = responses.front();
				responses.pop_front();
				return response;
			};
		}
	};

	DirectoryHttpResponse Registered(std::string a_id = "live-abc", int a_heartbeat = 30)
	{
		return { 201, Json{ { "serverId", a_id }, { "secret", "s3cret" }, { "host", "203.0.113.10" }, { "heartbeatIntervalSeconds", a_heartbeat } }.dump(), {} };
	}

	DirectoryHttpResponse Error(int a_status, std::string a_code)
	{
		return { a_status, Json{ { "error", { { "code", a_code }, { "message", "nope" } } } }.dump(), {} };
	}

	DirectoryListing Listing()
	{
		return DirectoryListing{
			.name = "Test Server",
			.description = "desc",
			.port = 7777,
			.maximumPlayers = 4,
			.clientVersion = "0.9.0",
			.falloutRuntime = "1.10.163",
			.contentManifestFingerprint = std::nullopt,
			.capabilities = { "protocol-v2" },
			.passworded = false,
			.modpackFingerprint = std::nullopt
		};
	}

	void BuildsPasswordedAdvertisement()
	{
		FakeDirectory directory;
		std::atomic_size_t players{ 0 };
		auto passwordedListing = Listing();
		passwordedListing.passworded = true;
		const DirectoryRegistrar passworded{ "https://dir.example/api/v1/servers/", passwordedListing, players, directory.Sender() };
		assert(passworded.BuildRegistrationBody()["passworded"] == true);
		assert(passworded.BuildRegistrationBody()["listensOnIpv6"] == false);
		auto ipv6Listing = Listing();
		ipv6Listing.listensOnIpv6 = true;
		const DirectoryRegistrar ipv6{ "https://dir.example/api/v1/servers/", ipv6Listing, players, directory.Sender() };
		assert(ipv6.BuildRegistrationBody()["listensOnIpv6"] == true);
		const DirectoryRegistrar unlisted{ "https://dir.example/api/v1/servers/", Listing(), players, directory.Sender() };
		assert(unlisted.BuildRegistrationBody()["passworded"] == false);
	}

	void BuildsModpackAdvertisement()
	{
		FakeDirectory directory;
		std::atomic_size_t players{ 0 };
		const DirectoryRegistrar unrequired{ "https://dir.example/api/v1/servers/", Listing(), players, directory.Sender() };
		assert(!unrequired.BuildRegistrationBody()["compatibility"].contains("modpackFingerprint"));
		assert(unrequired.BuildRegistrationBody()["compatibility"]["capabilities"] == Json::array({ "protocol-v2" }));
		auto modpackListing = Listing();
		modpackListing.modpackFingerprint = std::string(64, 'c');
		const DirectoryRegistrar modpacked{ "https://dir.example/api/v1/servers/", modpackListing, players, directory.Sender() };
		const auto body = modpacked.BuildRegistrationBody();
		assert(body["compatibility"]["modpackFingerprint"] == std::string(64, 'c'));
		assert(body["compatibility"]["capabilities"] == Json::array({ "protocol-v2", "exact_modpack_v1" }));
	}

	void RegistersThenHeartbeatsWithLiveCounts()
	{
		FakeDirectory directory;
		std::atomic_size_t players{ 1 };
		DirectoryRegistrar registrar{ "https://dir.example/api/v1/servers/", Listing(), players, directory.Sender() };
		assert(registrar.Status().state == DirectoryState::Registering);

		directory.responses.push_back(Registered("live-abc", 20));
		assert(registrar.Tick() == 20s);
		assert(registrar.Status().state == DirectoryState::Registered);
		assert(registrar.Status().serverId == "live-abc");
		assert(registrar.Status().publicAddress == "203.0.113.10:7777");

		const auto& registration = directory.calls.back();
		assert(registration.method == "POST");
		assert(registration.url == "https://dir.example/api/v1/servers/register");
		assert(registration.bearer.empty());
		assert(registration.body["port"] == 7777);
		assert(registration.body["maxPlayers"] == 4);
		assert(registration.body["players"] == 1);
		assert(registration.body["passworded"] == false);
		assert(!registration.body.contains("host"));
		assert(!registration.body.contains("address"));
		assert(registration.body["compatibility"]["falloutRuntime"] == "1.10.163");
		assert(!registration.body["compatibility"].contains("contentManifestFingerprint"));

		players = 3;
		directory.responses.push_back({ 200, Json{ { "heartbeatIntervalSeconds", 1 } }.dump(), {} });
		assert(registrar.Tick() == DirectoryRegistrar::MinimumHeartbeat);
		const auto& heartbeat = directory.calls.back();
		assert(heartbeat.url == "https://dir.example/api/v1/servers/live-abc/heartbeat");
		assert(heartbeat.bearer == "s3cret");
		assert(heartbeat.body["players"] == 3);

		players = 99;
		directory.responses.push_back({ 200, "{}", {} });
		(void)registrar.Tick();
		assert(directory.calls.back().body["players"] == 4);
	}

	void ReRegistersWhenListingExpires()
	{
		FakeDirectory directory;
		std::atomic_size_t players{ 0 };
		DirectoryRegistrar registrar{ "https://dir.example/api/v1/servers", Listing(), players, directory.Sender() };
		directory.responses.push_back(Registered("live-one"));
		(void)registrar.Tick();

		directory.responses.push_back(Error(404, "unknown_server"));
		assert(registrar.Tick() == 0s);
		assert(registrar.Status().state == DirectoryState::Registering);
		assert(registrar.Status().serverId.empty());

		directory.responses.push_back(Registered("live-two"));
		(void)registrar.Tick();
		assert(directory.calls.back().url.ends_with("/register"));
		assert(registrar.Status().serverId == "live-two");
		assert(registrar.Status().state == DirectoryState::Registered);
	}

	void BacksOffWhenDirectoryIsDown()
	{
		FakeDirectory directory;
		std::atomic_size_t players{ 0 };
		DirectoryRegistrar registrar{ "https://dir.example/api/v1/servers", Listing(), players, directory.Sender() };

		directory.responses.push_back({ 0, {}, "connection refused" });
		assert(registrar.Tick() == 5s);
		assert(registrar.Status().state == DirectoryState::Error);
		assert(registrar.Status().error.find("connection refused") != std::string::npos);

		directory.responses.push_back({ 503, "", {} });
		assert(registrar.Tick() == 10s);
		directory.responses.push_back(Error(429, "rate_limited"));
		assert(registrar.Tick() == 20s);
		for (int attempt = 0; attempt < 10; ++attempt) {
			directory.responses.push_back({ 502, "", {} });
			assert(registrar.Tick() <= DirectoryRegistrar::MaximumBackoff);
		}
		assert(registrar.Status().consecutiveFailures == 13);

		directory.responses.push_back(Registered());
		(void)registrar.Tick();
		assert(registrar.Status().state == DirectoryState::Registered);
		assert(registrar.Status().consecutiveFailures == 0);
		assert(registrar.Status().error.empty());

		directory.responses.push_back({ 0, {}, "timeout" });
		assert(registrar.Tick() == 5s);
		assert(registrar.Status().state == DirectoryState::Error);
		assert(registrar.Status().serverId == "live-abc");
		directory.responses.push_back({ 200, "{}", {} });
		(void)registrar.Tick();
		assert(directory.calls.back().url.ends_with("/live-abc/heartbeat"));
		assert(registrar.Status().state == DirectoryState::Registered);
	}

	void SurfacesRejectionCodes()
	{
		FakeDirectory directory;
		std::atomic_size_t players{ 0 };
		DirectoryRegistrar registrar{ "https://dir.example/api/v1/servers", Listing(), players, directory.Sender() };

		directory.responses.push_back(Error(422, "non_public_address"));
		assert(registrar.Tick() == DirectoryRegistrar::RejectedRetry);
		const auto status = registrar.Status();
		assert(status.state == DirectoryState::Error);
		assert(status.error.find("non_public_address") != std::string::npos);
		assert(status.error.find("422") != std::string::npos);
		assert(CommonwealthOnline::Server::ToJson(status)["state"] == "error");
	}

	void RejectsMalformedRegistration()
	{
		FakeDirectory directory;
		std::atomic_size_t players{ 0 };
		DirectoryRegistrar registrar{ "https://dir.example/api/v1/servers", Listing(), players, directory.Sender() };

		directory.responses.push_back({ 201, "not json", {} });
		(void)registrar.Tick();
		assert(registrar.Status().state == DirectoryState::Error);

		directory.responses.push_back({ 201, Json{ { "serverId", "../../admin" }, { "secret", "x" } }.dump(), {} });
		(void)registrar.Tick();
		assert(registrar.Status().state == DirectoryState::Error);
		assert(registrar.Status().serverId.empty());
	}

	void SurvivesMalformedErrorMetadata()
	{
		FakeDirectory directory;
		std::atomic_size_t players{ 0 };
		DirectoryRegistrar registrar{ "https://dir.example/api/v1/servers", Listing(), players, directory.Sender() };
		for (const auto& error : { Json{ { "code", 7 }, { "message", "no" } }, Json{ { "code", "bad" }, { "message", Json::array() } } }) {
			directory.responses.push_back({ 422, Json{ { "error", error } }.dump(), {} });
			assert(registrar.Tick() == DirectoryRegistrar::RejectedRetry);
			assert(registrar.Status().state == DirectoryState::Error);
			assert(registrar.Status().error.find("422") != std::string::npos);
		}
		directory.responses.push_back(Registered());
		(void)registrar.Tick();
		directory.responses.push_back({ 503, R"({"error":{"code":false,"message":{}}})", {} });
		assert(registrar.Tick() == 5s);
		assert(registrar.Status().serverId == "live-abc");
	}

	void RejectsUnsafeBearerSecrets()
	{
		for (const auto& secret : { std::string{ "s\r\nX-Injected: yes" }, std::string{ "s\0x", 3 }, std::string{ "s x" }, std::string{ "s\x7f" } }) {
			FakeDirectory directory;
			std::atomic_size_t players{ 0 };
			DirectoryRegistrar registrar{ "https://dir.example/api/v1/servers", Listing(), players, directory.Sender() };
			directory.responses.push_back({ 201, Json{ { "serverId", "live-abc" }, { "secret", secret } }.dump(), {} });
			assert(registrar.Tick() == 5s);
			assert(registrar.Status().state == DirectoryState::Error);
			assert(registrar.Status().serverId.empty());
		}
	}

	void UnregistersOnShutdown()
	{
		FakeDirectory directory;
		std::atomic_size_t players{ 0 };
		DirectoryRegistrar registrar{ "https://dir.example/api/v1/servers", Listing(), players, directory.Sender() };
		directory.responses.push_back(Registered());
		(void)registrar.Tick();

		directory.responses.push_back({ 204, "", {} });
		registrar.Unregister();
		assert(directory.calls.back().method == "DELETE");
		assert(directory.calls.back().url == "https://dir.example/api/v1/servers/live-abc");
		assert(directory.calls.back().bearer == "s3cret");
		assert(registrar.Status().state == DirectoryState::Disabled);

		const auto callCount = directory.calls.size();
		registrar.Unregister();
		assert(directory.calls.size() == callCount);
	}

	struct BlockingDirectory : std::enable_shared_from_this<BlockingDirectory>
	{
		std::mutex mutex;
		std::condition_variable changed;
		bool entered{};
		bool released{};
		int completed{};
		std::atomic_int calls{};

		CommonwealthOnline::Server::DirectoryHttpSender Sender()
		{
			auto self = shared_from_this();
			return [self](std::string_view, const std::string&, const std::string&, const std::string&) {
				self->calls.fetch_add(1);
				std::unique_lock lock{ self->mutex };
				self->entered = true;
				self->changed.notify_all();
				self->changed.wait_for(lock, 10s, [self] { return self->released; });
				++self->completed;
				return DirectoryHttpResponse{ 0, {}, "released by the test" };
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

	void StopsWithoutWaitingForAnInFlightRequest()
	{
		auto directory = std::make_shared<BlockingDirectory>();
		std::atomic_size_t players{ 0 };
		{
			DirectoryRegistrar registrar{ "https://dir.example/api/v1/servers", Listing(), players, directory->Sender() };
			registrar.Start();
			assert(directory->WaitForEntry());

			const auto started = std::chrono::steady_clock::now();
			registrar.Stop();
			const auto elapsed = std::chrono::steady_clock::now() - started;
			assert(elapsed < 2s);
			assert(directory->calls.load() == 1);
			assert(registrar.Status().state == DirectoryState::Registering);

			directory->Release();
			assert(directory->WaitForCompletion());
			std::this_thread::sleep_for(200ms);

			assert(directory->calls.load() == 1);
			const auto status = registrar.Status();
			assert(status.state == DirectoryState::Registering);
			assert(status.serverId.empty());
			assert(status.error.empty());
			assert(status.consecutiveFailures == 0);
		}
		directory->Release();
	}

	void ThreadStartsAndStopsPromptly()
	{
		FakeDirectory directory;
		std::atomic_size_t players{ 0 };
		DirectoryRegistrar registrar{ "https://dir.example/api/v1/servers", Listing(), players, directory.Sender() };
		directory.responses.push_back(Registered("live-abc", 300));
		directory.responses.push_back({ 204, "", {} });
		const auto started = std::chrono::steady_clock::now();
		registrar.Start();
		while (registrar.Status().state != DirectoryState::Registered) {
			std::this_thread::sleep_for(5ms);
			assert(std::chrono::steady_clock::now() - started < 5s);
		}
		registrar.Stop();
		assert(std::chrono::steady_clock::now() - started < 5s);
		assert(directory.calls.back().method == "DELETE");
	}

	void HandlesSupabaseEdgeFunctionResponses()
	{
		const std::string base = "https://oeajjfquvmdxqqtiotfa.supabase.co/functions/v1/server-directory";
		const std::string id = "live-" + std::string(32, 'f');
		FakeDirectory directory;
		std::atomic_size_t players{ 0 };
		DirectoryRegistrar registrar{ base, Listing(), players, directory.Sender() };

		directory.responses.push_back(Error(429, "rate_limited"));
		assert(registrar.Tick() == 5s);
		assert(registrar.Status().error.find("rate_limited") != std::string::npos);

		directory.responses.push_back(Error(503, "directory_unavailable"));
		assert(registrar.Tick() == 10s);

		directory.responses.push_back({ 201,
			Json{ { "serverId", id }, { "secret", std::string(64, 'a') }, { "host", "185.141.119.153" }, { "port", 7777 },
				{ "heartbeatIntervalSeconds", 30 }, { "ttlSeconds", 90 } }.dump(),
			{} });
		assert(registrar.Tick() == 30s);
		assert(directory.calls.back().url == base + "/register");
		assert(!directory.calls.back().body.contains("host"));
		assert(registrar.Status().publicAddress == "185.141.119.153:7777");

		players = 3;
		directory.responses.push_back({ 200, Json{ { "serverId", id }, { "players", 3 }, { "maxPlayers", 4 }, { "heartbeatIntervalSeconds", 30 }, { "ttlSeconds", 90 } }.dump(), {} });
		assert(registrar.Tick() == 30s);
		assert(directory.calls.back().url == base + "/" + id + "/heartbeat");
		assert(directory.calls.back().bearer == std::string(64, 'a'));
		assert(directory.calls.back().body["players"] == 3);

		directory.responses.push_back(Error(404, "unknown_server"));
		assert(registrar.Tick() == 0s);
		assert(registrar.Status().state == DirectoryState::Registering);

		directory.responses.push_back(Error(422, "invalid_metadata"));
		assert(registrar.Tick() == DirectoryRegistrar::RejectedRetry);
		assert(registrar.Status().error.find("invalid_metadata") != std::string::npos);
	}
}

int main()
{
	RegistersThenHeartbeatsWithLiveCounts();
	BuildsPasswordedAdvertisement();
	BuildsModpackAdvertisement();
	ReRegistersWhenListingExpires();
	BacksOffWhenDirectoryIsDown();
	SurfacesRejectionCodes();
	RejectsMalformedRegistration();
	SurvivesMalformedErrorMetadata();
	RejectsUnsafeBearerSecrets();
	UnregistersOnShutdown();
	StopsWithoutWaitingForAnInFlightRequest();
	ThreadStartsAndStopsPromptly();
	HandlesSupabaseEdgeFunctionResponses();
	return 0;
}
