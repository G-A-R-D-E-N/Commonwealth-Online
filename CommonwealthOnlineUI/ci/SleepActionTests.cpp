#include "F4TSleepAction.h"

#include <cassert>

int main()
{
	using namespace F4T::SleepView;
	const auto valid = ParseAction(R"({"op":"sleep","token":5,"hours":8})", Phase::Choose, 5, 24);
	assert(valid && valid->action == Action::Sleep && valid->hours == 8);
	assert(!ParseAction(R"({"op":"sleep","token":4,"hours":8})", Phase::Choose, 5, 24));
	assert(!ParseAction(R"({"op":"sleep","token":5,"hours":25})", Phase::Choose, 5, 24));
	assert(!ParseAction(R"({"op":"sleep","token":5,"hours":1})", Phase::Resting, 5, 24));
	assert(!ParseAction(R"({"op":"cancel","token":5,"hours":1})", Phase::Closed, 5, 24));
	assert(!ParseAction(R"({"op":"sleep","token":5,"hours":-1})", Phase::Choose, 5, 24));
	assert(!ParseAction("{", Phase::Choose, 5, 24));
	const auto cancel = ParseAction(R"({"op":"cancel","token":5,"hours":1})", Phase::Resting, 5, 24);
	assert(cancel && cancel->action == Action::Cancel);
}
