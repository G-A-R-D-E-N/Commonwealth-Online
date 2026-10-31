#include "PersistenceSmokeSeed.h"

#include <cassert>
#include <string>

int main()
{
	using namespace CommonwealthOnline::Server::Testing;
	using namespace std::chrono_literals;
	const auto path = std::filesystem::temp_directory_path() /
		("co-seed-commit-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".json");
	const auto write = [&](const std::string& text) {
		std::ofstream output(path, std::ios::binary | std::ios::trunc);
		output << text;
		output.close();
		assert(output.good());
	};
	const std::string committed = R"({"profiles":{"primary":{"playerState":{"characterName":"Survivor"}}}})";
	assert(!WaitForSeedCommit(path, "primary", "Survivor", 0ms));
	for (const auto& invalid : {
		"{", "[]", R"({"profiles":[]})", R"({"profiles":{"primary":null}})",
		R"({"profiles":{"primary":{"resumeTokenHash":"identity-only"}}})",
		R"({"profiles":{"other":{"playerState":{"characterName":"Survivor"}}}})",
		R"({"profiles":{"primary":{"playerState":{"characterName":123}}}})",
		R"({"profiles":{"primary":{"playerState":{"characterName":"Old Survivor"}}}})" }) {
		write(invalid);
		assert(!WaitForSeedCommit(path, "primary", "Survivor", 0ms));
	}
	std::thread writer([&] {
		std::this_thread::sleep_for(50ms);
		write(committed);
	});
	const bool observed = WaitForSeedCommit(path, "primary", "Survivor", 2s);
	writer.join();
	assert(observed);
	assert(WaitForSeedCommit(path, "primary", "Survivor", 0ms));
	std::filesystem::remove(path);
	assert(!WaitForSeedCommit(path, "primary", "Survivor", 20ms));
}
