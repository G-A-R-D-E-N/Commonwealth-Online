#include "OpenSslTokenService.h"

#include <array>
#include <cstdint>
#include <limits>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#ifdef _WIN32
#  define WIN32_LEAN_AND_MEAN
#  include <Windows.h>
#  include <bcrypt.h>
#else
#  include <openssl/rand.h>
#  include <openssl/sha.h>
#endif

namespace CommonwealthOnline::Server
{
	namespace
	{
		std::string Base64Url(std::span<const unsigned char> a_bytes)
		{
			static constexpr char alphabet[] =
				"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
			std::string output;
			output.reserve((a_bytes.size() * 4 + 2) / 3);
			std::size_t index = 0;
			while (index + 3 <= a_bytes.size()) {
				const auto value =
					(static_cast<std::uint32_t>(a_bytes[index]) << 16) |
					(static_cast<std::uint32_t>(a_bytes[index + 1]) << 8) |
					static_cast<std::uint32_t>(a_bytes[index + 2]);
				output.push_back(alphabet[(value >> 18) & 0x3f]);
				output.push_back(alphabet[(value >> 12) & 0x3f]);
				output.push_back(alphabet[(value >> 6) & 0x3f]);
				output.push_back(alphabet[value & 0x3f]);
				index += 3;
			}
			const auto remaining = a_bytes.size() - index;
			if (remaining == 1) {
				const auto value = static_cast<std::uint32_t>(a_bytes[index]) << 16;
				output.push_back(alphabet[(value >> 18) & 0x3f]);
				output.push_back(alphabet[(value >> 12) & 0x3f]);
			} else if (remaining == 2) {
				const auto value =
					(static_cast<std::uint32_t>(a_bytes[index]) << 16) |
					(static_cast<std::uint32_t>(a_bytes[index + 1]) << 8);
				output.push_back(alphabet[(value >> 18) & 0x3f]);
				output.push_back(alphabet[(value >> 12) & 0x3f]);
				output.push_back(alphabet[(value >> 6) & 0x3f]);
			}
			return output;
		}

#ifdef _WIN32
		void CheckNt(NTSTATUS a_status, std::string_view a_operation)
		{
			if (a_status < 0) {
				throw std::runtime_error(std::string{ a_operation } + " failed with NTSTATUS " + std::to_string(a_status));
			}
		}
#endif

		void FillRandom(std::span<unsigned char> a_bytes)
		{
			if (a_bytes.empty()) {
				return;
			}
#ifdef _WIN32
			if (a_bytes.size() > static_cast<std::size_t>((std::numeric_limits<ULONG>::max)())) {
				throw std::length_error("Secure random request exceeds BCryptGenRandom limits");
			}
			CheckNt(
				BCryptGenRandom(
					nullptr,
					a_bytes.data(),
					static_cast<ULONG>(a_bytes.size()),
					BCRYPT_USE_SYSTEM_PREFERRED_RNG),
				"BCryptGenRandom");
#else
			if (a_bytes.size() > static_cast<std::size_t>((std::numeric_limits<int>::max)())) {
				throw std::length_error("Secure random request exceeds OpenSSL RAND_bytes limits");
			}
			if (RAND_bytes(a_bytes.data(), static_cast<int>(a_bytes.size())) != 1) {
				throw std::runtime_error("OpenSSL RAND_bytes failed while generating secure random bytes");
			}
#endif
		}

		std::string LowerHex(std::span<const unsigned char> a_bytes)
		{
			static constexpr char hex[] = "0123456789abcdef";
			std::string output(a_bytes.size() * 2, '0');
			for (std::size_t index = 0; index < a_bytes.size(); ++index) {
				output[index * 2] = hex[(a_bytes[index] >> 4) & 0x0f];
				output[index * 2 + 1] = hex[a_bytes[index] & 0x0f];
			}
			return output;
		}
	}

	std::string OpenSslTokenService::CreateToken()
	{
		std::array<unsigned char, 32> randomBytes{};
		FillRandom(randomBytes);
		return Base64Url(randomBytes);
	}

	std::string OpenSslTokenService::CreateRandomHex(std::size_t a_byteCount)
	{
		std::vector<unsigned char> randomBytes(a_byteCount);
		FillRandom(randomBytes);
		return LowerHex(randomBytes);
	}

	std::string OpenSslTokenService::HmacSha256Hex(std::string_view a_key, std::string_view a_message)
	{
		constexpr std::size_t kBlockBytes = 64;
		if (a_key.size() > kBlockBytes) {
			throw std::invalid_argument("HMAC key exceeds the SHA-256 block size.");
		}
		std::string innerPad(kBlockBytes, '\x36');
		std::string outerPad(kBlockBytes, '\x5c');
		for (std::size_t index = 0; index < a_key.size(); ++index) {
			innerPad[index] = static_cast<char>(innerPad[index] ^ a_key[index]);
			outerPad[index] = static_cast<char>(outerPad[index] ^ a_key[index]);
		}
		innerPad.append(a_message);
		const auto innerHash = HashToken(innerPad);
		outerPad.append(reinterpret_cast<const char*>(innerHash.data()), innerHash.size());
		const auto outerHash = HashToken(outerPad);
		return LowerHex(outerHash);
	}

	F4T::Identity::TokenHash OpenSslTokenService::HashToken(std::string_view a_token)
	{
		F4T::Identity::TokenHash hash{};
#ifdef _WIN32
		BCRYPT_ALG_HANDLE algorithm = nullptr;
		BCRYPT_HASH_HANDLE hashHandle = nullptr;
		try {
			CheckNt(
				BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0),
				"BCryptOpenAlgorithmProvider(SHA256)");

			ULONG objectLength = 0;
			ULONG resultLength = 0;
			CheckNt(
				BCryptGetProperty(
					algorithm,
					BCRYPT_OBJECT_LENGTH,
					reinterpret_cast<PUCHAR>(&objectLength),
					sizeof(objectLength),
					&resultLength,
					0),
				"BCryptGetProperty(BCRYPT_OBJECT_LENGTH)");
			std::vector<unsigned char> object(objectLength);
			CheckNt(
				BCryptCreateHash(
					algorithm,
					&hashHandle,
					object.data(),
					static_cast<ULONG>(object.size()),
					nullptr,
					0,
					0),
				"BCryptCreateHash");
			CheckNt(
				BCryptHashData(
					hashHandle,
					reinterpret_cast<PUCHAR>(const_cast<char*>(a_token.data())),
					static_cast<ULONG>(a_token.size()),
					0),
				"BCryptHashData");
			CheckNt(
				BCryptFinishHash(hashHandle, hash.data(), static_cast<ULONG>(hash.size()), 0),
				"BCryptFinishHash");
		} catch (...) {
			if (hashHandle != nullptr) {
				BCryptDestroyHash(hashHandle);
			}
			if (algorithm != nullptr) {
				BCryptCloseAlgorithmProvider(algorithm, 0);
			}
			throw;
		}
		BCryptDestroyHash(hashHandle);
		BCryptCloseAlgorithmProvider(algorithm, 0);
#else
		if (SHA256(
				reinterpret_cast<const unsigned char*>(a_token.data()),
				a_token.size(),
				hash.data()) == nullptr) {
			throw std::runtime_error("OpenSSL SHA-256 failed while hashing a resume token");
		}
#endif
		return hash;
	}
}
