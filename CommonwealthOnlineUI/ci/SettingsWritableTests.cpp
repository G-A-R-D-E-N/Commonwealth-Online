#include "F4TSettingsWritable.h"

#include <cassert>
#include <string>
#include <unordered_map>

int main()
{
	struct State
	{
		bool available{ false };
	};
	using F4T::Settings::IsSettingWritable;

	std::unordered_map<std::string, State> states;
	assert(!IsSettingWritable(states, "gameplay.difficulty"));

	states["gameplay.difficulty"] = State{ false };
	assert(!IsSettingWritable(states, "gameplay.difficulty"));

	states["gameplay.difficulty"] = State{ true };
	assert(IsSettingWritable(states, "gameplay.difficulty"));
	assert(!IsSettingWritable(states, "interface.hudColour"));

	return 0;
}
