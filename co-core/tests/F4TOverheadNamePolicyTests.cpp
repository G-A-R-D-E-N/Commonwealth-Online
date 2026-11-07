#include "F4TOverheadNamePolicy.h"

#include <cassert>
#include <cmath>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace
{
	using F4T::OverheadNamePolicy::Candidate;
	using F4T::OverheadNamePolicy::FormatLabel;
	using F4T::OverheadNamePolicy::OpacityFor;
	using F4T::OverheadNamePolicy::Select;
	using F4T::OverheadNamePolicy::Settings;

	Candidate Make(std::uint32_t a_id, float a_distance, std::string a_name = "Alice", std::string a_tag = "AT")
	{
		Candidate candidate;
		candidate.playerId = a_id;
		candidate.distance = a_distance;
		candidate.displayName = std::move(a_name);
		candidate.factionTag = std::move(a_tag);
		return candidate;
	}
}

int main()
{
	assert(FormatLabel("AT", "Alice") == "[AT] Alice");
	assert(FormatLabel("", "Alice") == "Alice");
	assert(FormatLabel("ABCDEFGHI", "Alice") == "Alice");
	assert(FormatLabel("  ", "Alice") == "Alice");
	assert(FormatLabel("AT", "").empty());
	assert(FormatLabel("<b>", "<i>x</i>") == "[<b>] <i>x</i>");

	const Settings settings{};
	assert(OpacityFor(0.0f, settings) == 1.0f);
	assert(OpacityFor(1200.0f, settings) == 1.0f);
	assert(std::fabs(OpacityFor(1600.0f, settings) - 0.5f) < 1e-6f);
	assert(OpacityFor(2000.0f, settings) == 0.0f);
	assert(OpacityFor(5000.0f, settings) == 0.0f);
	assert(OpacityFor(-1.0f, settings) == 0.0f);
	assert(OpacityFor(std::nanf(""), settings) == 0.0f);

	{
		Settings off{};
		off.showNameplates = false;
		assert(Select({ Make(1, 10.0f) }, off).empty());
	}

	{
		Settings none{};
		none.maxLabels = 0;
		assert(Select({ Make(1, 10.0f) }, none).empty());
	}

	{
		Candidate downed = Make(1, 10.0f);
		downed.downed = true;
		Candidate asleep = Make(2, 10.0f);
		asleep.sleeping = true;
		Candidate nameless = Make(3, 10.0f, "");
		const auto labels = Select({ downed, asleep, nameless, Make(4, 2500.0f), Make(5, 10.0f, "Bob", "") }, Settings{});
		assert(labels.size() == 1);
		assert(labels.front().playerId == 5);
		assert(labels.front().text == "Bob");
	}

	{
		const auto labels = Select({ Make(3, 300.0f), Make(1, 100.0f), Make(2, 100.0f) }, Settings{});
		assert(labels.size() == 3);
		assert(labels.at(0).playerId == 1);
		assert(labels.at(1).playerId == 2);
		assert(labels.at(2).playerId == 3);
		assert(labels.at(0).text == "[AT] Alice");
		assert(labels.at(0).opacity == 1.0f);
	}

	{
		Settings capped{};
		capped.maxLabels = 2;
		std::vector<Candidate> crowd;
		for (std::uint32_t id = 1; id <= 5; ++id) {
			crowd.push_back(Make(id, static_cast<float>(id) * 100.0f));
		}
		const auto labels = Select(crowd, capped);
		assert(labels.size() == 2);
		assert(labels.at(0).playerId == 1);
		assert(labels.at(1).playerId == 2);
	}

	{
		const auto labels = Select({ Make(1, 1600.0f) }, Settings{});
		assert(labels.size() == 1);
		assert(std::fabs(labels.front().opacity - 0.5f) < 1e-6f);
	}

	return 0;
}
