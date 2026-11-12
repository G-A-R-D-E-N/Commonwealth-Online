#pragma once

#include "F4TObjectState.h"

#include <cstdint>
#include <string>

namespace CommonwealthOnline::Server::TextHelpers
{
	const char* ObjectStateRejectReason(F4T::ObjectState::Outcome a_outcome) noexcept;
	std::string Ipv4Text(std::uint32_t a_hostOrder);
}
