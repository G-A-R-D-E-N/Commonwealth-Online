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

	const Json campWorkshop{ { "camps", Json::array({ Json{ { "id", 1 } } }) } };
	const Json campSettlements = Json::array({ Json{ { "buildZoneId", 1 } } });

	void SeedCamp(DurableStateFileStore& a_store)
	{
		F4T::Identity::TokenHash hash{};
		a_store.SaveResumeCredential("camp-owner", hash, std::nullopt, true, 5000);
		(void)a_store.InitializeInventory("camp-owner", Json::array({ Json{ { "plugin", "CommonwealthOnline.esp" }, { "localFormId", "997F" }, { "count", 1u } } }));
		const Json drop{ { "type", "inventoryDropRequest" }, { "operationId", "kit" }, { "expectedRevision", 0u },
			{ "itemKey", "commonwealthonline.esp:0000997F" }, { "count", 1u } };
		Require(a_store.DropInventory("camp-owner", drop, F4T::Drops::InventoryPosition{ "CAFE", "F00D", 0, 0, 0 }).outcome == F4T::Drops::Outcome::Committed, "kit drop failed");
	}

	void VerifyCamp(DurableStateFileStore& a_store, bool a_created)
	{
		const bool dropPresent = !a_store.GetLiveDrop(F4T::Drops::kFirstDroppedEntityId).is_null();
		Require(dropPresent != a_created, "kit state mismatch");
		Require(a_store.GetWorkshop() == (a_created ? campWorkshop : Json::object()), "workshop state mismatch");
		Require(a_store.GetSettlementAccess() == (a_created ? campSettlements : Json::array()), "settlement state mismatch");
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
		(mode != "seed" && mode != "update" && mode != "verify" && mode != "camp-seed" && mode != "camp-update" && mode != "camp-verify") ||
		((mode == "update" || mode == "camp-update") && stage != "temp-synced" && stage != "backup-replaced" && stage != "primary-replaced") ||
		((mode == "verify" || mode == "camp-verify") && stage != "old" && stage != "new")) return 2;
	try {
		{
			DurableStateFileStore store(argv[2], [] { return 1000.; }, [] { return serverId; }, [] {
				CommonwealthOnline::Server::DurableCommitCrashPause("primary-replaced");
			});
			if (mode == "camp-seed") {
				SeedCamp(store);
				VerifyCamp(store, false);
			} else if (mode == "camp-update") {
				VerifyCamp(store, false);
				const auto drop = store.GetLiveDrop(F4T::Drops::kFirstDroppedEntityId);
				armedStage = stage;
				(void)store.CommitCamp(F4T::Drops::kFirstDroppedEntityId, drop, campWorkshop, campSettlements, "camp-owner",
					F4T::Drops::kFirstDroppedEntityId, {});
				throw std::runtime_error("selected commit boundary was not reached");
			} else if (mode == "camp-verify") {
				VerifyCamp(store, stage == "new");
			} else if (mode == "seed") {
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
