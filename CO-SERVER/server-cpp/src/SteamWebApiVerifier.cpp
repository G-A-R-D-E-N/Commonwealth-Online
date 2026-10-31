#include "SteamWebApiVerifier.h"

#include "AdminTokenStore.h"
#include "SteamIdentityLink.h"

#include "F4TSteamId.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cstdlib>
#include <exception>
#include <utility>

namespace CommonwealthOnline::Server
{
	namespace
	{
		using Json = nlohmann::json;

		constexpr std::string_view kAuthenticateUrl = "https://api.steampowered.com/ISteamUserAuth/AuthenticateUserTicket/v1/";
		constexpr std::string_view kFallout4AppId = "377160";
		constexpr std::string_view kDummyTicket = "00";

		std::string AuthenticateUrl(std::string_view a_key, std::string_view a_ticketHex)
		{
			std::string url{ kAuthenticateUrl };
			url.append("?key=").append(a_key);
			url.append("&appid=").append(kFallout4AppId);
			url.append("&ticket=").append(a_ticketHex);
			return url;
		}

		std::optional<std::string> ReadEnvironment(std::string_view a_name)
		{
			const std::string name{ a_name };
#ifdef _WIN32
			char* value = nullptr;
			std::size_t length = 0;
			if (_dupenv_s(&value, &length, name.c_str()) != 0 || value == nullptr) {
				return std::nullopt;
			}
			std::string result{ value };
			std::free(value);
			return result;
#else
			const char* value = std::getenv(name.c_str());
			return value != nullptr ? std::optional<std::string>{ value } : std::nullopt;
#endif
		}

		std::string Trimmed(std::string_view a_text)
		{
			const auto first = a_text.find_first_not_of(" \t\r\n");
			if (first == std::string_view::npos) return {};
			const auto last = a_text.find_last_not_of(" \t\r\n");
			return std::string{ a_text.substr(first, last - first + 1) };
		}

		std::string ConnectionLabel(std::uint32_t a_connectionId)
		{
			return "connection " + std::to_string(a_connectionId);
		}
	}

	bool IsSteamWebApiKey(std::string_view a_key) noexcept
	{
		return a_key.size() == 32 && std::all_of(a_key.begin(), a_key.end(), [](char a_character) {
			return (a_character >= '0' && a_character <= '9') ||
				(a_character >= 'A' && a_character <= 'F') ||
				(a_character >= 'a' && a_character <= 'f');
		});
	}

	std::optional<SteamWebApiKey> LoadSteamWebApiKey(const std::filesystem::path& a_baseDirectory, std::string& a_error)
	{
		a_error.clear();
		if (const auto environment = ReadEnvironment(kSteamWebApiKeyEnvironment)) {
			auto key = Trimmed(*environment);
			if (!key.empty()) {
				if (!IsSteamWebApiKey(key)) {
					a_error = std::string{ kSteamWebApiKeyEnvironment } + " is set but is not a 32-character Steam Web API key";
					return std::nullopt;
				}
				return SteamWebApiKey{ std::move(key), std::string{ kSteamWebApiKeyEnvironment } };
			}
		}
		const auto path = a_baseDirectory / kSteamWebApiKeyFileName;
		try {
			auto stored = AdminTokenStore::TryLoad(path);
			if (!stored) {
				return std::nullopt;
			}
			if (!IsSteamWebApiKey(*stored)) {
				a_error = path.string() + " does not hold a 32-character Steam Web API key";
				return std::nullopt;
			}
			return SteamWebApiKey{ std::move(*stored), path.string() };
		} catch (const std::exception& exception) {
			a_error = exception.what();
			return std::nullopt;
		}
	}

	SteamKeyCheckResult CheckSteamWebApiKey(std::string_view a_key, const DirectoryHttpSender& a_sender)
	{
		if (!IsSteamWebApiKey(a_key)) {
			return { SteamKeyCheck::Rejected, "the key is not 32 hex characters" };
		}
		if (!a_sender) {
			return { SteamKeyCheck::Unreachable, "no HTTP client is available" };
		}
		const auto response = a_sender("GET", AuthenticateUrl(a_key, kDummyTicket), {}, {});
		if (!response.transportError.empty()) {
			return { SteamKeyCheck::Unreachable, "Steam could not be reached: " + response.transportError };
		}
		if (response.status == 401 || response.status == 403) {
			return { SteamKeyCheck::Rejected, "HTTP " + std::to_string(response.status) };
		}
		if (response.status != 200) {
			return { SteamKeyCheck::Unreachable, "Steam answered HTTP " + std::to_string(response.status) };
		}
		const auto body = Json::parse(response.body, nullptr, false);
		if (!body.is_object() || !body.contains("response")) {
			return { SteamKeyCheck::Unreachable, "Steam sent an unexpected answer" };
		}
		return { SteamKeyCheck::Accepted, {} };
	}

	SteamTicketVerdict InterpretSteamTicketResponse(const DirectoryHttpResponse& a_response)
	{
		SteamTicketVerdict verdict;
		if (!a_response.transportError.empty()) {
			verdict.detail = "Steam could not be reached: " + a_response.transportError;
			return verdict;
		}
		if (a_response.status == 401 || a_response.status == 403) {
			verdict.detail = "Steam refused the server's Web API key (HTTP " + std::to_string(a_response.status) + ")";
			return verdict;
		}
		if (a_response.status == 429) {
			verdict.detail = "Steam rate-limited the server's Web API key";
			return verdict;
		}
		if (a_response.status != 200) {
			verdict.detail = "Steam answered HTTP " + std::to_string(a_response.status);
			return verdict;
		}
		const auto body = Json::parse(a_response.body, nullptr, false);
		const auto* response = body.is_object() && body.contains("response") && body["response"].is_object()
			? &body["response"] : nullptr;
		if (response == nullptr) {
			verdict.detail = "Steam sent an unexpected answer";
			return verdict;
		}
		if (const auto error = response->find("error"); error != response->end() && error->is_object()) {
			verdict.outcome = SteamTicketOutcome::Retry;
			const auto code = error->value("errorcode", 0);
			verdict.detail = "Steam refused the ticket (error " + std::to_string(code) + ")";
			return verdict;
		}
		const auto params = response->find("params");
		if (params == response->end() || !params->is_object()) {
			verdict.detail = "Steam sent an unexpected answer";
			return verdict;
		}
		const auto result = params->value("result", std::string{});
		const auto steamId = params->value("steamid", std::string{});
		if (result != "OK" || !F4T::SteamId::ParseIndividual(steamId)) {
			verdict.outcome = SteamTicketOutcome::Rejected;
			verdict.detail = "Steam did not confirm the ticket";
			return verdict;
		}
		verdict.outcome = SteamTicketOutcome::Verified;
		verdict.steamId = steamId;
		return verdict;
	}

	SteamTicketAction DecideSteamTicketAction(
		const SteamTicketVerdict& a_verdict,
		const std::function<std::optional<std::string>(std::string_view)>& a_banLookup)
	{
		const auto connection = ConnectionLabel(a_verdict.connectionId);
		switch (a_verdict.outcome) {
		case SteamTicketOutcome::Verified:
			if (!a_verdict.claimedSteamId.empty() && a_verdict.claimedSteamId != a_verdict.steamId) {
				return { true, "steam_identity_mismatch", "The Steam ID sent by this client does not match its Steam account.",
					"Ended " + connection + ": the claimed Steam ID does not match the " +
						std::string{ SteamIdSourceName(SteamIdSource::WebApi) } + " Steam ID." };
			}
			if (a_banLookup) {
				if (const auto banReason = a_banLookup(a_verdict.steamId)) {
					return { true, "steam_banned", *banReason, "Ended " + connection + ": its verified Steam ID is banned." };
				}
			}
			return { false, {}, {}, "Steam ID for " + connection + " verified through the Steam Web API." };
		case SteamTicketOutcome::Rejected:
		case SteamTicketOutcome::Retry:
			return { false, {}, {}, "Steam ID for " + connection + " stays unverified: " + a_verdict.detail + "." };
		case SteamTicketOutcome::Unavailable:
			break;
		}
		return { false, {}, {}, "Steam ID check for " + connection + " unavailable: " + a_verdict.detail + "." };
	}

	SteamWebApiVerifier::SteamWebApiVerifier(std::string a_key, DirectoryHttpSender a_sender) :
		SteamWebApiVerifier(std::move(a_key), std::move(a_sender), Options{})
	{}

	SteamWebApiVerifier::SteamWebApiVerifier(std::string a_key, DirectoryHttpSender a_sender, Options a_options) :
		_state(std::make_shared<State>())
	{
		_state->key = std::move(a_key);
		_state->sender = std::move(a_sender);
		_state->options = a_options;
		_state->options.attempts = (std::max)(_state->options.attempts, std::uint32_t{ 1 });
		auto state = _state;
		_thread = std::thread([state] { Run(state); });
	}

	SteamWebApiVerifier::~SteamWebApiVerifier()
	{
		Stop();
	}

	void SteamWebApiVerifier::Stop()
	{
		(void)StopWorker(_thread, _state->exit);
	}

	bool SteamWebApiVerifier::Submit(std::uint32_t a_connectionId, std::string_view a_ticketHex, std::string_view a_claimedSteamId, std::string_view a_profileId, std::uint64_t a_sourceKey)
	{
		if (!F4T::SteamId::IsTicketHex(a_ticketHex)) {
			return false;
		}
		{
			std::lock_guard lock{ _state->exit.mutex };
			if (_state->exit.stopping || _state->queue.size() + _state->inFlight >= _state->options.maximumPending) {
				return false;
			}
			if (a_sourceKey != 0) {
				auto& pending = _state->pendingBySource[a_sourceKey];
				if (pending >= _state->options.maximumPendingPerSource) {
					if (pending == 0) {
						_state->pendingBySource.erase(a_sourceKey);
					}
					return false;
				}
				++pending;
			}
			_state->queue.push_back(Job{
				a_connectionId,
				std::string{ a_ticketHex },
				std::string{ a_claimedSteamId },
				std::string{ a_profileId },
				0,
				std::chrono::steady_clock::now(),
				a_sourceKey });
		}
		_state->exit.wake.notify_all();
		return true;
	}

	std::vector<SteamTicketVerdict> SteamWebApiVerifier::TakeResults()
	{
		std::lock_guard lock{ _state->exit.mutex };
		return std::exchange(_state->results, {});
	}

	void SteamWebApiVerifier::Run(std::shared_ptr<State> a_state)
	{
		{
			std::unique_lock lock{ a_state->exit.mutex };
			while (!a_state->exit.stopping) {
				if (a_state->queue.empty()) {
					a_state->exit.wake.wait(lock);
					continue;
				}
				const auto ready = std::min_element(a_state->queue.begin(), a_state->queue.end(), [](const Job& a_left, const Job& a_right) {
					return a_left.notBefore < a_right.notBefore;
				});
				if (ready->notBefore > std::chrono::steady_clock::now()) {
					a_state->exit.wake.wait_until(lock, ready->notBefore);
					continue;
				}
				auto job = std::move(*ready);
				a_state->queue.erase(ready);
				++a_state->inFlight;
				lock.unlock();

				SteamTicketVerdict verdict;
				try {
					verdict = InterpretSteamTicketResponse(
						a_state->sender ? a_state->sender("GET", AuthenticateUrl(a_state->key, job.ticketHex), {}, {})
						                : DirectoryHttpResponse{ .status = 0, .body = {}, .transportError = "no HTTP client is available" });
				} catch (const std::exception& exception) {
					verdict = SteamTicketVerdict{};
					verdict.detail = "Steam sent an answer this server could not read: " + std::string{ exception.what() };
				} catch (...) {
					verdict = SteamTicketVerdict{};
					verdict.detail = "Steam sent an answer this server could not read";
				}

				lock.lock();
				a_state->inFlight -= 1;
				if (a_state->exit.abandoned.load(std::memory_order_acquire)) {
					break;
				}
				verdict.connectionId = job.connectionId;
				verdict.claimedSteamId = job.claimedSteamId;
				verdict.profileId = job.profileId;
				if (verdict.outcome == SteamTicketOutcome::Retry && job.attempt + 1 < a_state->options.attempts && !a_state->exit.stopping) {
					++job.attempt;
					job.notBefore = std::chrono::steady_clock::now() + a_state->options.retryDelay;
					a_state->queue.push_back(std::move(job));
					continue;
				}
				if (verdict.outcome == SteamTicketOutcome::Retry) {
					verdict.outcome = SteamTicketOutcome::Rejected;
				}
				if (job.sourceKey != 0) {
					if (const auto pending = a_state->pendingBySource.find(job.sourceKey);
						pending != a_state->pendingBySource.end() && --pending->second == 0) {
						a_state->pendingBySource.erase(pending);
					}
				}
				a_state->results.push_back(std::move(verdict));
			}
		}
		MarkWorkerExited(a_state->exit);
	}
}
