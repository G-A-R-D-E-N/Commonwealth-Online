#pragma once

#include <nlohmann/json.hpp>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <thread>

namespace CommonwealthOnline::Server::Testing
{
	inline bool SeedCommitted(const std::filesystem::path& a_path, std::string_view a_profile, std::string_view a_name)
	{
		std::ifstream input(a_path, std::ios::binary);
		if (!input) return false;
		const auto root = nlohmann::json::parse(input, nullptr, false);
		if (!root.is_object()) return false;
		const auto profiles = root.find("profiles");
		if (profiles == root.end() || !profiles->is_object()) return false;
		const auto profile = profiles->find(std::string{ a_profile });
		if (profile == profiles->end() || !profile->is_object()) return false;
		const auto state = profile->find("playerState");
		if (state == profile->end() || !state->is_object()) return false;
		const auto name = state->find("characterName");
		return name != state->end() && name->is_string() && name->get_ref<const std::string&>() == a_name;
	}

	inline bool WaitForSeedCommit(const std::filesystem::path& a_path, std::string_view a_profile,
		std::string_view a_name, std::chrono::milliseconds a_timeout)
	{
		const auto deadline = std::chrono::steady_clock::now() + a_timeout;
		for (;;) {
			if (SeedCommitted(a_path, a_profile, a_name)) return true;
			if (std::chrono::steady_clock::now() >= deadline) return false;
			std::this_thread::sleep_for(std::chrono::milliseconds(10));
		}
	}
}
