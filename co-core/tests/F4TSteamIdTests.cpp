#include "F4TSteamId.h"

#include <cassert>
#include <cstdint>
#include <string>

namespace
{
	void TestIndividualRange()
	{
		assert(!F4T::SteamId::IsIndividual(0));
		assert(!F4T::SteamId::IsIndividual(F4T::SteamId::kIndividualBase));
		assert(F4T::SteamId::IsIndividual(F4T::SteamId::kIndividualBase + 1));
		assert(F4T::SteamId::IsIndividual(76561198000000000ull));
		assert(F4T::SteamId::IsIndividual(F4T::SteamId::kIndividualMaximum));
		assert(!F4T::SteamId::IsIndividual(F4T::SteamId::kIndividualMaximum + 1));
		assert(!F4T::SteamId::IsIndividual(0x00000000FFFFFFFFull));
		assert(!F4T::SteamId::IsIndividual(103582791429521408ull));
	}

	void TestParseAcceptsIndividualDecimal()
	{
		assert(F4T::SteamId::ParseIndividual("76561197960265729") == 76561197960265729ull);
		assert(F4T::SteamId::ParseIndividual("76561198000000000") == 76561198000000000ull);
		assert(F4T::SteamId::ParseIndividual("76561202255233023") == 76561202255233023ull);
		assert(F4T::SteamId::ParseIndividual(std::to_string(F4T::SteamId::kIndividualMaximum)).has_value());
	}

	void TestParseRejectsMalformed()
	{
		for (const char* text : {
				 "",
				 "7656119796026572",
				 "765611979602657290",
				 "76561197960265728",
				 "76561202255233024",
				 "76561190000000000",
				 "00000000000000001",
				 "+7656119800000000",
				 "-7656119800000000",
				 "7656119800000000a",
				 "7656119800000000 ",
				 " 7656119800000000",
				 "103582791429521408",
				 "4294967295" }) {
			assert(!F4T::SteamId::ParseIndividual(text).has_value());
		}
	}

	void TestTicketHex()
	{
		const std::uint8_t bytes[] = { 0x00, 0x1F, 0xA0, 0xFF };
		assert(F4T::SteamId::TicketHex(bytes, sizeof(bytes)) == "001FA0FF");
		assert(F4T::SteamId::IsTicketHex("001FA0FF"));
		assert(F4T::SteamId::IsTicketHex("001fa0ff"));
		assert(F4T::SteamId::IsTicketHex(std::string(F4T::SteamId::kMaximumTicketBytes * 2, 'A')));
		assert(!F4T::SteamId::IsTicketHex(""));
		assert(!F4T::SteamId::IsTicketHex("ABC"));
		assert(!F4T::SteamId::IsTicketHex("0G"));
		assert(!F4T::SteamId::IsTicketHex("00 1F"));
		assert(!F4T::SteamId::IsTicketHex("00&key=x"));
		assert(!F4T::SteamId::IsTicketHex(std::string(F4T::SteamId::kMaximumTicketBytes * 2 + 2, 'A')));
	}
}

int main()
{
	TestIndividualRange();
	TestParseAcceptsIndividualDecimal();
	TestParseRejectsMalformed();
	TestTicketHex();
	return 0;
}
