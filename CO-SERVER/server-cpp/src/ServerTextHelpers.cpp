#include "ServerTextHelpers.h"

#include <cstdint>
#include <string>

namespace CommonwealthOnline::Server::TextHelpers
{
	const char* ObjectStateRejectReason(F4T::ObjectState::Outcome a_outcome) noexcept
	{
		using F4T::ObjectState::Outcome;
		switch (a_outcome) {
		case Outcome::Applied:
			return nullptr;
		case Outcome::RejectedCapacity:
			return "objectCapacity";
		case Outcome::RejectedUnknownObject:
			return "unknownObject";
		case Outcome::RejectedKindMismatch:
			return "kindMismatch";
		case Outcome::RejectedPositionDrift:
			return "positionMismatch";
		case Outcome::RejectedActionNotApplicable:
			return "actionNotApplicable";
		case Outcome::RejectedOperationConflict:
			return "operationConflict";
		case Outcome::RejectedPlayerCapacity:
			return "objectCapacityExceeded";
		}
		return "invalidRequest";
	}

	std::string Ipv4Text(std::uint32_t a_hostOrder)
	{
		return std::to_string((a_hostOrder >> 24) & 0xff) + "." +
			std::to_string((a_hostOrder >> 16) & 0xff) + "." +
			std::to_string((a_hostOrder >> 8) & 0xff) + "." +
			std::to_string(a_hostOrder & 0xff);
	}
}
