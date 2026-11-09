#pragma once

#include "RE/A/Actor.h"

#include <string_view>

namespace F4T::Downed::Script
{
	bool CallActorBool(RE::Actor& a_actor, std::string_view a_function, bool a_value);
}
