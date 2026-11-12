#include "F4TObjectModSnapshot.h"

#include <array>
#include <cassert>

struct RawEntry
{
	std::uint32_t objectID;
	std::uint8_t index;
	std::uint8_t rank;
	std::uint8_t disabled;
};

int main()
{
	using namespace F4T::ObjectModSnapshot;
	const std::array source{ RawEntry{ 1u, 2u, 3u, 0u }, RawEntry{ 2u, 4u, 5u, 7u } };
	const auto resolve = [](std::uint32_t id) -> std::optional<std::string> {
		return std::string{ id == 1 ? "test.esm:00000001" : "other.esl:00000002" };
	};
	const auto captured = Capture(std::span{ source }, resolve);
	assert(captured && captured->size() == 2);
	assert((captured->at(0) == Entry{ "test.esm:00000001", 2u, 3u, 0u }));
	assert((captured->at(1) == Entry{ "other.esl:00000002", 4u, 5u, 7u }));
	assert(!Capture(std::span{ source }, [](std::uint32_t) -> std::optional<std::string> { return std::nullopt; }));
	assert(!Capture(std::span{ source }, [](std::uint32_t) -> std::optional<std::string> { return "FF000001"; }));
	std::vector<RawEntry> oversized(MaximumEntries + 1);
	assert(!Capture(std::span{ oversized }, resolve));
}
