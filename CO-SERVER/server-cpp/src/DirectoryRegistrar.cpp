#include "DirectoryRegistrar.h"

#include <algorithm>
#include <format>
#include <iostream>
#include <utility>

namespace CommonwealthOnline::Server
{
	namespace
	{
		using Json = nlohmann::json;

		std::string DescribeFailure(const DirectoryHttpResponse& a_response)
		{
			if (!a_response.transportError.empty()) {
				return std::format("Directory unreachable: {}", a_response.transportError);
			}
			const auto parsed = Json::parse(a_response.body, nullptr, false);
			if (parsed.is_object() && parsed.contains("error") && parsed["error"].is_object()) {
				const auto& error = parsed["error"];
				const auto code = error.contains("code") && error["code"].is_string()
					? error["code"].get<std::string>() : std::string{};
				const auto message = error.contains("message") && error["message"].is_string()
					? error["message"].get<std::string>() : std::string{};
				if (!code.empty()) {
					return std::format("{} (HTTP {}): {}", code, a_response.status, message);
				}
			}
			return std::format("Directory returned HTTP {}", a_response.status);
		}

		bool IsSafeServerId(std::string_view a_id)
		{
			if (a_id.empty() || a_id.size() > 64) {
				return false;
			}
			return std::ranges::all_of(a_id, [](char c) {
				return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_';
			});
		}

		std::chrono::seconds ClampHeartbeat(const Json& a_body)
		{
			const auto seconds = a_body.contains("heartbeatIntervalSeconds") && a_body["heartbeatIntervalSeconds"].is_number_integer()
				? a_body["heartbeatIntervalSeconds"].get<std::int64_t>()
				: DirectoryRegistrar::DefaultHeartbeat.count();
			return std::chrono::seconds{ std::clamp<std::int64_t>(
				seconds,
				DirectoryRegistrar::MinimumHeartbeat.count(),
				DirectoryRegistrar::MaximumHeartbeat.count()) };
		}

		std::unique_lock<std::mutex> LockExit(WorkerExit* a_exit)
		{
			return a_exit != nullptr ? std::unique_lock{ a_exit->mutex } : std::unique_lock<std::mutex>{};
		}

		bool Abandoned(const WorkerExit* a_exit)
		{
			return a_exit != nullptr && a_exit->abandoned.load(std::memory_order_acquire);
		}
	}

	std::string_view ToString(DirectoryState a_state) noexcept
	{
		switch (a_state) {
		case DirectoryState::Disabled:
			return "disabled";
		case DirectoryState::Registering:
			return "registering";
		case DirectoryState::Registered:
			return "registered";
		case DirectoryState::Error:
			return "error";
		}
		return "error";
	}

	Json ToJson(const DirectoryStatus& a_status)
	{
		return Json{
			{ "state", ToString(a_status.state) },
			{ "serverId", a_status.serverId },
			{ "publicAddress", a_status.publicAddress },
			{ "error", a_status.error },
			{ "consecutiveFailures", a_status.consecutiveFailures }
		};
	}

	DirectoryRegistrar::DirectoryRegistrar(
		std::string a_directoryUrl,
		DirectoryListing a_listing,
		const std::atomic_size_t& a_activePlayers,
		DirectoryHttpSender a_sender) :
		_state(std::make_shared<State>())
	{
		_state->directoryUrl = std::move(a_directoryUrl);
		_state->listing = std::move(a_listing);
		_state->activePlayers = &a_activePlayers;
		_state->sender = std::move(a_sender);
		while (!_state->directoryUrl.empty() && _state->directoryUrl.back() == '/') {
			_state->directoryUrl.pop_back();
		}
		_state->status.state = DirectoryState::Registering;
	}

	DirectoryRegistrar::~DirectoryRegistrar()
	{
		Stop();
	}

	Json DirectoryRegistrar::BuildRegistrationBody() const
	{
		const auto listing = ListingSnapshot();
		return RegistrationBody(*_state, listing, _state->activePlayers->load(std::memory_order_relaxed));
	}

	Json DirectoryRegistrar::BuildHeartbeatBody() const
	{
		const auto listing = ListingSnapshot();
		return HeartbeatBody(*_state, listing, _state->activePlayers->load(std::memory_order_relaxed));
	}

	DirectoryListing DirectoryRegistrar::ListingSnapshot() const
	{
		const std::scoped_lock lock{ _state->statusMutex };
		return _state->listing;
	}

	Json DirectoryRegistrar::RegistrationBody(const State& a_state, const DirectoryListing& a_listing, std::size_t a_players)
	{
		Json compatibility = Json::object();
		if (a_listing.clientVersion) compatibility["clientVersion"] = *a_listing.clientVersion;
		if (a_listing.falloutRuntime) compatibility["falloutRuntime"] = *a_listing.falloutRuntime;
		if (a_listing.contentManifestFingerprint) compatibility["contentManifestFingerprint"] = *a_listing.contentManifestFingerprint;
		auto capabilities = a_listing.capabilities;
		if (a_listing.modpackFingerprint) {
			compatibility["modpackFingerprint"] = *a_listing.modpackFingerprint;
			capabilities.push_back("exact_modpack_v1");
		}
		compatibility["capabilities"] = std::move(capabilities);

		auto body = HeartbeatBody(a_state, a_listing, a_players);
		body["name"] = a_listing.name;
		body["description"] = a_listing.description;
		body["port"] = a_listing.port;
		body["passworded"] = a_listing.passworded;
		body["listensOnIpv6"] = a_listing.listensOnIpv6;
		body["compatibility"] = std::move(compatibility);
		return body;
	}

	Json DirectoryRegistrar::HeartbeatBody(const State& a_state, const DirectoryListing& a_listing, std::size_t a_players)
	{
		(void)a_state;
		const auto maximum = a_listing.maximumPlayers;
		const auto players = (std::min)(a_players, maximum);
		return Json{
			{ "players", players },
			{ "maxPlayers", maximum }
		};
	}

	std::chrono::seconds DirectoryRegistrar::FailStatus(State& a_state, std::string a_error, bool a_rejected)
	{
		std::scoped_lock lock{ a_state.statusMutex };
		if (a_state.status.state != DirectoryState::Error || a_state.status.error != a_error) {
			std::cerr << "Public listing: " << a_error << '\n';
		}
		a_state.status.state = DirectoryState::Error;
		a_state.status.error = std::move(a_error);
		++a_state.status.consecutiveFailures;
		if (a_rejected) {
			return RejectedRetry;
		}
		const auto exponent = (std::min)(a_state.status.consecutiveFailures, 7u);
		return (std::min)(std::chrono::seconds{ 5LL << (exponent - 1) }, MaximumBackoff);
	}

	std::chrono::seconds DirectoryRegistrar::RegisterServer(State& a_state, std::size_t a_players, WorkerExit* a_exit)
	{
		{
			std::scoped_lock lock{ a_state.statusMutex };
			if (a_state.status.state != DirectoryState::Error) {
				a_state.status.state = DirectoryState::Registering;
			}
		}

		DirectoryListing listing;
		{
			const std::scoped_lock lock{ a_state.statusMutex };
			listing = a_state.listing;
		}
		const auto response = a_state.sender("POST", a_state.directoryUrl + "/register", {}, RegistrationBody(a_state, listing, a_players).dump());
		const auto exitLock = LockExit(a_exit);
		if (Abandoned(a_exit)) {
			return std::chrono::seconds{ 0 };
		}
		if (!response.transportError.empty() || response.status >= 500 || response.status == 429) {
			return FailStatus(a_state, DescribeFailure(response), false);
		}
		if (response.status != 200 && response.status != 201) {
			return FailStatus(a_state, DescribeFailure(response), true);
		}
		if (response.body.size() > MaximumResponseBytes) {
			return FailStatus(a_state, "Directory registration response was too large.", false);
		}

		const auto parsed = Json::parse(response.body, nullptr, false);
		if (!parsed.is_object() || !parsed.contains("serverId") || !parsed["serverId"].is_string() ||
			!parsed.contains("secret") || !parsed["secret"].is_string()) {
			return FailStatus(a_state, "Directory registration response was malformed.", false);
		}
		const auto serverId = parsed["serverId"].get<std::string>();
		auto secret = parsed["secret"].get<std::string>();
		if (!IsSafeServerId(serverId) || secret.empty() || secret.size() > 256 ||
			!std::ranges::all_of(secret, [](unsigned char c) { return c > 32 && c < 127; })) {
			return FailStatus(a_state, "Directory registration response was malformed.", false);
		}

		a_state.secret = std::move(secret);
		a_state.heartbeat = ClampHeartbeat(parsed);
		std::scoped_lock lock{ a_state.statusMutex };
		if (a_state.status.state != DirectoryState::Registered) {
			std::cerr << "Public listing registered as " << serverId << '\n';
		}
		a_state.status.state = DirectoryState::Registered;
		a_state.status.serverId = serverId;
		a_state.status.publicAddress = parsed.contains("host") && parsed["host"].is_string()
			? std::format("{}:{}", parsed["host"].get<std::string>(), listing.port)
			: std::string{};
		a_state.status.error.clear();
		a_state.status.consecutiveFailures = 0;
		return a_state.heartbeat;
	}

	std::chrono::seconds DirectoryRegistrar::SendHeartbeat(State& a_state, std::size_t a_players, WorkerExit* a_exit)
	{
		std::string serverId;
		{
			std::scoped_lock lock{ a_state.statusMutex };
			serverId = a_state.status.serverId;
		}

		const auto secret = a_state.secret;
		DirectoryListing listing;
		{
			const std::scoped_lock lock{ a_state.statusMutex };
			listing = a_state.listing;
		}
		const auto response = a_state.sender("POST", std::format("{}/{}/heartbeat", a_state.directoryUrl, serverId), secret, HeartbeatBody(a_state, listing, a_players).dump());
		const auto exitLock = LockExit(a_exit);
		if (Abandoned(a_exit)) {
			return std::chrono::seconds{ 0 };
		}
		if (response.transportError.empty() && response.status == 200) {
			const auto parsed = Json::parse(response.body, nullptr, false);
			if (parsed.is_object()) {
				a_state.heartbeat = ClampHeartbeat(parsed);
			}
			std::scoped_lock lock{ a_state.statusMutex };
			a_state.status.state = DirectoryState::Registered;
			a_state.status.error.clear();
			a_state.status.consecutiveFailures = 0;
			return a_state.heartbeat;
		}

		if (response.transportError.empty() && (response.status == 401 || response.status == 403 || response.status == 404)) {
			a_state.secret.clear();
			std::scoped_lock lock{ a_state.statusMutex };
			a_state.status.serverId.clear();
			a_state.status.state = DirectoryState::Registering;
			a_state.status.error = DescribeFailure(response);
			return std::chrono::seconds{ 0 };
		}

		return FailStatus(a_state, DescribeFailure(response), false);
	}

	std::chrono::seconds DirectoryRegistrar::Tick()
	{
		return TickServer(*_state, _state->activePlayers->load(std::memory_order_relaxed), nullptr);
	}

	std::chrono::seconds DirectoryRegistrar::TickServer(State& a_state, std::size_t a_players, WorkerExit* a_exit)
	{
		bool registered = false;
		{
			std::scoped_lock lock{ a_state.statusMutex };
			registered = !a_state.status.serverId.empty();
		}
		return registered ? SendHeartbeat(a_state, a_players, a_exit) : RegisterServer(a_state, a_players, a_exit);
	}

	void DirectoryRegistrar::Unregister()
	{
		std::string serverId;
		std::string secret;
		{
			std::scoped_lock lock{ _state->statusMutex };
			serverId = std::exchange(_state->status.serverId, {});
			secret = std::exchange(_state->secret, {});
			_state->status.state = DirectoryState::Disabled;
		}
		if (serverId.empty() || secret.empty()) {
			return;
		}
		(void)_state->sender("DELETE", std::format("{}/{}", _state->directoryUrl, serverId), secret, {});
	}

	void DirectoryRegistrar::Start()
	{
		if (_thread.joinable()) {
			return;
		}
		_exit = std::make_shared<WorkerExit>();
		auto state = _state;
		auto exit = _exit;
		_thread = std::thread([state, exit] { Worker(state, exit); });
	}

	void DirectoryRegistrar::Stop()
	{
		if (!_exit) {
			return;
		}
		if (StopWorker(_thread, *_exit)) {
			Unregister();
		}
	}

	void DirectoryRegistrar::Worker(std::shared_ptr<State> a_state, std::shared_ptr<WorkerExit> a_exit)
	{
		for (;;) {
			std::size_t players{};
			{
				std::scoped_lock lock{ a_exit->mutex };
				if (a_exit->stopping) {
					break;
				}
				players = a_state->activePlayers->load(std::memory_order_relaxed);
			}
			const auto delay = TickServer(*a_state, players, a_exit.get());
			std::unique_lock lock{ a_exit->mutex };
			if (a_exit->wake.wait_for(lock, (std::max)(delay, std::chrono::seconds{ 1 }), [&a_exit] { return a_exit->stopping; })) {
				break;
			}
		}
		MarkWorkerExited(*a_exit);
	}

	DirectoryStatus DirectoryRegistrar::Status() const
	{
		std::scoped_lock lock{ _state->statusMutex };
		return _state->status;
	}

	void DirectoryRegistrar::UpdateListing(DirectoryListing a_listing)
	{
		const std::scoped_lock lock{ _state->statusMutex };
		_state->listing = std::move(a_listing);
	}
}
