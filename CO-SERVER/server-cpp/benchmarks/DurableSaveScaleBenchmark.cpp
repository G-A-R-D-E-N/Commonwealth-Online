#include "DurableStateFileStore.h"
#include "F4TInventorySnapshot.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <charconv>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace
{
using namespace CommonwealthOnline::Server;
using Json = nlohmann::json;
using Clock = std::chrono::steady_clock;

void Check(bool a_ok, const char* a_message)
{
	if (!a_ok) throw std::runtime_error(a_message);
}

int Number(const char* a_text, int a_minimum, int a_maximum)
{
	int value{};
	const std::string_view input(a_text);
	const auto [end, error] = std::from_chars(input.data(), input.data() + input.size(), value);
	Check(error == std::errc{} && end == input.data() + input.size() && value >= a_minimum && value <= a_maximum, "argument outside allowed range");
	return value;
}

double Timed(auto&& a_operation)
{
	const auto start = Clock::now();
	a_operation();
	return std::chrono::duration<double, std::milli>(Clock::now() - start).count();
}

Json Summary(std::vector<double> a_values)
{
	std::sort(a_values.begin(), a_values.end());
	const auto at = [&](double a_fraction) { return a_values.at(static_cast<std::size_t>(std::ceil(a_fraction * a_values.size())) - 1); };
	return { { "count", a_values.size() }, { "p50_ms", at(.50) }, { "p95_ms", at(.95) }, { "max_ms", a_values.back() } };
}

std::string Hex(std::size_t a_value, std::size_t a_width)
{
	static constexpr char digits[] = "0123456789abcdef";
	std::string text(a_width, '0');
	for (std::size_t index = a_width; index-- > 0; a_value >>= 4) text[index] = digits[a_value & 15];
	return text;
}

std::string Account(int a_index)
{
	return "co-" + Hex(static_cast<std::size_t>(a_index) + 1, 32);
}

Json Catalog(int a_index, int a_variant)
{
	Json catalog = Json::array();
	for (int item = 0; item < 7; ++item) {
		std::string form = Hex(0x1000 + item * 17 + a_index, 8);
		std::transform(form.begin(), form.end(), form.begin(), [](char a_char) { return static_cast<char>(std::toupper(a_char)); });
		catalog.push_back(Json{ { "plugin", "fallout4.esm" }, { "localFormId", form }, { "count", 1 + item + a_variant } });
	}
	return catalog;
}

void Seed(DurableStateFileStore& a_store, int a_profiles, double& a_now)
{
	F4T::Identity::TokenHash hash{};
	hash.fill(7);
	Json morphs = Json::array();
	for (int value = 0; value < 110; ++value) morphs.push_back(Json{ { "id", Hex(0x20000 + value, 8) }, { "value", 0.001 * ((value * 11) % 997) + 0.0001234 } });
	for (int index = 0; index < a_profiles; ++index) {
		const auto account = Account(index);
		const auto slot = account + "#0";
		a_now += 1;
		a_store.SaveResumeCredential(account, hash, std::nullopt, true, 1.0e9);
		(void)a_store.InitializeInventory(slot, Json::array());
		for (int revision = 0; revision < 10; ++revision) {
			const auto request = F4T::InventorySnapshot::NormalizeRequest(Hex(index * 100 + revision, 32), static_cast<std::uint32_t>(revision), Catalog(index, revision + 1));
			Check(request && a_store.ReconcileInventory(slot, *request).changed, "seed reconcile refused");
		}
		Json appearance{ { "version", 1 }, { "morphs", morphs } };
		Check(a_store.UpdatePlayerState(slot, Json{ { "type", "playerState" }, { "characterName", "Survivor " + std::to_string(index) }, { "appearance", appearance } }), "seed player state refused");
	}
	a_store.WaitForCommits();
}

Json Run(const std::filesystem::path& a_directory, int a_profiles, int a_samples)
{
	Check(std::filesystem::create_directory(a_directory), "output directory must be new");
	const auto path = a_directory / "durable-state.json";
	const std::string instance = "0123456789abcdef0123456789abcdef";
	double now = 1000;
	DurableStateFileStore store(path, [&] { return now; }, [&] { return instance; });
	store.SetDeferCommits(true);
	Seed(store, a_profiles, now);
	Json result{ { "profiles", a_profiles }, { "samples", a_samples }, { "seed_bytes", std::filesystem::file_size(path) } };
	{
		Json root;
		std::ifstream input(path, std::ios::binary);
		input >> root;
		result["profiles_compact_bytes"] = root.at("profiles").dump().size();
	}
	const auto measure = [&](const char* a_name, auto&& a_operation) {
		std::vector<double> main, commit;
		for (int sample = 0; sample < a_samples; ++sample) {
			now += 1;
			main.push_back(Timed([&] { a_operation(sample); }));
			commit.push_back(Timed([&] { store.WaitForCommits(); }));
		}
		result[a_name] = Json{ { "main_thread", Summary(main) }, { "writer_drain", Summary(commit) } };
	};
	measure("update_player_state", [&](int a_sample) {
		Check(store.UpdatePlayerState(Account(a_sample % a_profiles) + "#0",
			Json{ { "type", "playerState" }, { "characterName", "Renamed " + std::to_string(a_sample) } }), "player state not saved");
	});
	measure("update_player_states_batch_of_4", [&](int a_sample) {
		std::unordered_map<std::string, Json> batch;
		for (int offset = 0; offset < 4; ++offset)
			batch[Account((a_sample * 4 + offset) % a_profiles) + "#0"] = Json{ { "type", "playerState" }, { "characterName", "Batch " + std::to_string(a_sample) } };
		Check(store.UpdatePlayerStates(batch), "player states not saved");
	});
	measure("save_resume_credential", [&](int a_sample) {
		F4T::Identity::TokenHash hash{};
		hash.fill(static_cast<std::uint8_t>(a_sample + 1));
		store.SaveResumeCredential(Account(a_sample % a_profiles), hash, std::nullopt, true, 1.0e9);
	});
	measure("survival_needs_one_profile", [&](int a_sample) {
		Check(store.MutateSurvivalNeeds({ Account(a_sample % a_profiles) + "#0" }, [&](std::string_view, F4T::SurvivalNeeds::Record& a_record) {
			a_record.state.hunger = 0.5 + 0.001 * a_sample;
			return true;
		}, false), "survival needs not saved");
	});
	store.WaitForCommits();
	Check(!store.TakePersistenceWarning(), "persistence warning");
	result["final_bytes"] = std::filesystem::file_size(path);
	{
		DurableStateFileStore reopened(path, [&] { return now; }, [&] { return instance; });
		for (int index = 0; index < a_profiles; ++index) Check(reopened.GetPlayerState(Account(index) + "#0").has_value(), "reopen lost a profile");
		Check(reopened.GetPlayerState(Account(0) + "#0")->at("characterName").get<std::string>().starts_with("Batch"), "reopen lost the last update");
	}
	result["checks_passed"] = true;
	return result;
}
}

int main(int a_argc, char** a_argv)
{
	try {
		Check(a_argc == 4, "usage: DurableSaveScaleBenchmark NEW_DIRECTORY PROFILES SAMPLES");
		std::cout << Run(a_argv[1], Number(a_argv[2], 1, 4096), Number(a_argv[3], 2, 10000)).dump(2) << '\n';
		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
