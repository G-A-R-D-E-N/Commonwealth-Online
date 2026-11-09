#include "OpenSslTokenService.h"

#include <array>
#include <cassert>
#include <cctype>
#include <cstdint>
#include <string>

int main()
{
	using CommonwealthOnline::Server::OpenSslTokenService;

	OpenSslTokenService tokens;
	const auto token = tokens.CreateToken();
	assert(token.size() == 43);
	for (const unsigned char ch : token) {
		assert(std::isalnum(ch) != 0 || ch == '-' || ch == '_');
	}

	const auto identifier = tokens.CreateRandomHex(16);
	assert(identifier.size() == 32);
	for (const unsigned char ch : identifier) {
		assert((ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'f'));
	}

	const auto hash = tokens.HashToken("abc");
	constexpr std::array<std::uint8_t, 32> expected{
		0xba, 0x78, 0x16, 0xbf, 0x8f, 0x01, 0xcf, 0xea,
		0x41, 0x41, 0x40, 0xde, 0x5d, 0xae, 0x22, 0x23,
		0xb0, 0x03, 0x61, 0xa3, 0x96, 0x17, 0x7a, 0x9c,
		0xb4, 0x10, 0xff, 0x61, 0xf2, 0x00, 0x15, 0xad
	};
	assert(hash == expected);

	assert(tokens.HmacSha256Hex(std::string(20, '\x0b'), "Hi There") ==
		"b0344c61d8db38535ca8afceaf0bf12b881dc200c9833da726e9376c2e32cff7");
	assert(tokens.HmacSha256Hex("Jefe", "what do ya want for nothing?") ==
		"5bdcc146bf60754e6a042426089575c75a003f089d2739839dec58b964ec3843");
	return 0;
}
