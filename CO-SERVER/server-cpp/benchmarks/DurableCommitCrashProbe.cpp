#include "DurableStateFileStore.h"

#include <charconv>
#include <chrono>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>

namespace
{
	using CommonwealthOnline::Server::DurableStateFileStore;
	using Json = nlohmann::json;
	std::string armedStage;
	constexpr auto serverId = "0123456789abcdef0123456789abcdef";

	void Require(bool a_ok, const char* a_message)
	{
		if (!a_ok) throw std::runtime_error(a_message);
	}

	F4T::Identity::TokenHash Hash(unsigned a_index, bool a_previous = false)
	{
		F4T::Identity::TokenHash hash{};
		hash.fill(static_cast<std::uint8_t>(a_index + (a_previous ? 64 : 1)));
		return hash;
	}

	std::unordered_map<std::string, Json> States(unsigned a_count, std::string_view a_prefix)
	{
		std::unordered_map<std::string, Json> states;
		for (unsigned index = 0; index < a_count; ++index)
			states.emplace("crash-profile-" + std::to_string(index), Json{ { "type", "playerState" }, { "characterName", std::string{ a_prefix } + std::to_string(index) } });
		return states;
	}

	void Verify(DurableStateFileStore& a_store, unsigned a_count, std::string_view a_prefix)
	{
		Require(a_store.ServerInstanceId() == serverId && !a_store.RecoveryNotice(), "unexpected server identity or backup recovery");
		Require(a_store.GetServerWorldState().at("timeHHmm") == "1200", "world state changed");
		for (const auto& [profile, expected] : States(a_count, a_prefix)) {
			const auto state = a_store.GetPlayerState(profile);
			Require(state && *state == expected, "batch player state mismatch");
		}
		for (unsigned index = 0; index < a_count; ++index) {
			const auto credential = a_store.GetResumeCredential("crash-profile-" + std::to_string(index));
			Require(credential && credential->tokenHash == Hash(index) && credential->previousTokenHash == Hash(index, true) &&
				credential->confirmed && credential->expiresAt == 5000, "resume credential changed");
		}
	}
}

namespace CommonwealthOnline::Server
{
	void DurableCommitCrashPause(std::string_view a_stage)
	{
		if (armedStage != a_stage) return;
		std::cout << "CO_DURABLE_COMMIT_STAGE " << a_stage << std::endl;
		for (;;) std::this_thread::sleep_for(std::chrono::milliseconds(10));
	}
}

int main(int argc, char** argv)
{
	if (argc != 5) return 2;
	unsigned count{};
	const std::string_view mode{ argv[1] }, number{ argv[3] }, stage{ argv[4] };
	const auto [end, error] = std::from_chars(number.data(), number.data() + number.size(), count);
	if (error != std::errc{} || end != number.data() + number.size() || count < 1 || count > 16 ||
		(mode != "seed" && mode != "update" && mode != "verify") ||
		(mode == "update" && stage != "temp-synced" && stage != "backup-replaced" && stage != "primary-replaced") ||
		(mode == "verify" && stage != "old" && stage != "new")) return 2;
	try {
		{
			DurableStateFileStore store(argv[2], [] { return 1000.; }, [] { return serverId; }, [] {
				CommonwealthOnline::Server::DurableCommitCrashPause("primary-replaced");
			});
			if (mode == "seed") {
				for (unsigned index = 0; index < count; ++index)
					store.SaveResumeCredential("crash-profile-" + std::to_string(index), Hash(index), Hash(index, true), true, 5000);
				Require(store.UpdatePlayerStates(States(count, "old-")), "seed batch unchanged");
				store.UpdateServerWorldState(Json{ { "timeHHmm", "1200" } });
				Verify(store, count, "old-");
			} else if (mode == "update") {
				Verify(store, count, "old-");
				armedStage = stage;
				Require(store.UpdatePlayerStates(States(count, "new-")), "update batch unchanged");
				throw std::runtime_error("selected commit boundary was not reached");
			} else {
				Verify(store, count, std::string{ stage } + "-");
				Require(store.UpdatePlayerStates(States(count, "continued-")), "continuation commit unchanged");
			}
		}
		if (mode == "verify") {
			DurableStateFileStore reopened(argv[2], [] { return 1000.; }, [] { return "ffffffffffffffffffffffffffffffff"; });
			Verify(reopened, count, "continued-");
		}
		std::cout << "CO_DURABLE_COMMIT_PASS " << mode << ' ' << count << std::endl;
		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
