#include "F4TQuestStoryOrder.h"
#include "ServerConfiguration.h"

#include <nlohmann/json.hpp>

#include <cassert>
#include <cstddef>
#include <filesystem>
#include <fstream>

int main()
{
	using namespace CommonwealthOnline::Server::Configuration;
	using Json = nlohmann::json;

	const std::filesystem::path sampleDirectory{ CO_SERVER_SAMPLE_DIR };

	std::ifstream nativeFile{ sampleDirectory / "commonwealth-server-native.json" };
	assert(nativeFile);
	const auto native = Parse(Json::parse(nativeFile), sampleDirectory);
	assert(F4T::ServerConfiguration::Validate(native).empty());
	assert(!native.questStoryOrderPath);
	assert(!native.questStoryOrder);
	assert(native.factions.maximumFactions == 512);
	assert(native.factions.maximumMembers == 16);
	assert(native.factions.pendingInvites == 256);
	assert(native.factions.mutationRefillPerSecond == 1.0);
	assert(F4T::ServerConfiguration::DownedConfig(native).downedSeconds == 60);
	assert(F4T::ServerConfiguration::DownedConfig(native).respawnGraceSeconds == 8.0);

	std::ifstream sampleFile{ sampleDirectory / "quest-story-order.sample.json" };
	assert(sampleFile);
	const auto sample = F4T::QuestStory::Order::Load(Json::parse(sampleFile));
	assert(sample);
	const auto& builtin = F4T::QuestStory::Order::Default();
	assert(sample->Size() == builtin.Size());
	assert(sample->Rows().size() == builtin.Rows().size());
	for (std::size_t index = 0; index < builtin.Rows().size(); ++index) {
		const auto& expected = builtin.Rows()[index];
		const auto& actual = sample->Rows()[index];
		assert(actual.label == expected.label);
		assert(actual.source == expected.source);
		assert(actual.prerequisites.size() == expected.prerequisites.size());
	}

	const auto configured = Parse(Json{ { "questStoryOrderPath", "quest-story-order.sample.json" } }, sampleDirectory);
	assert(configured.questStoryOrderPath && *configured.questStoryOrderPath == "quest-story-order.sample.json");
	assert(configured.questStoryOrder);
	assert(configured.questStoryOrder->Size() == builtin.Size());

	return 0;
}
