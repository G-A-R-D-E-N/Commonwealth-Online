#include "ServerTextHelpers.h"

#include <cassert>
#include <cstdint>
#include <string_view>

using namespace CommonwealthOnline::Server::TextHelpers;
using F4T::ObjectState::Outcome;

namespace
{
	void TestObjectStateRejectReason()
	{
		assert(ObjectStateRejectReason(Outcome::Applied) == nullptr);
		assert(std::string_view{ ObjectStateRejectReason(Outcome::RejectedCapacity) } == "objectCapacity");
		assert(std::string_view{ ObjectStateRejectReason(Outcome::RejectedUnknownObject) } == "unknownObject");
		assert(std::string_view{ ObjectStateRejectReason(Outcome::RejectedKindMismatch) } == "kindMismatch");
		assert(std::string_view{ ObjectStateRejectReason(Outcome::RejectedPositionDrift) } == "positionMismatch");
		assert(std::string_view{ ObjectStateRejectReason(Outcome::RejectedActionNotApplicable) } == "actionNotApplicable");
		assert(std::string_view{ ObjectStateRejectReason(Outcome::RejectedOperationConflict) } == "operationConflict");
		assert(std::string_view{ ObjectStateRejectReason(Outcome::RejectedPlayerCapacity) } == "objectCapacityExceeded");
		assert(std::string_view{ ObjectStateRejectReason(static_cast<Outcome>(0xff)) } == "invalidRequest");
	}

	void TestIpv4Text()
	{
		assert(Ipv4Text(0x00000000U) == "0.0.0.0");
		assert(Ipv4Text(0x7F000001U) == "127.0.0.1");
		assert(Ipv4Text(0xFFFFFFFFU) == "255.255.255.255");
		assert(Ipv4Text(0xC0A80A01U) == "192.168.10.1");
	}
}

int main()
{
	TestObjectStateRejectReason();
	TestIpv4Text();
	return 0;
}
