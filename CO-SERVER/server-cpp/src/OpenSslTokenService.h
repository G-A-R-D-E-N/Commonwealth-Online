#pragma once

#include "F4TIdentityProtocol.h"

#include <cstddef>
#include <string>
#include <string_view>

namespace CommonwealthOnline::Server
{
	class OpenSslTokenService final : public F4T::Identity::TokenService
	{
	public:
		[[nodiscard]] std::string CreateToken() override;
		[[nodiscard]] F4T::Identity::TokenHash HashToken(std::string_view a_token) override;
		[[nodiscard]] std::string CreateRandomHex(std::size_t a_byteCount);
		[[nodiscard]] std::string HmacSha256Hex(std::string_view a_key, std::string_view a_message);
	};
}
