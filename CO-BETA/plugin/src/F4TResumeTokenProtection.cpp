#include "F4TResumeTokenProtection.h"

#ifdef _WIN32

#include <cstdint>
#include <memory>
#include <vector>

extern "C" __declspec(dllimport) int __stdcall W32_IMPL_CryptProtectData(void* a_dataIn, const wchar_t* a_description, void* a_entropy, void* a_reserved, void* a_prompt, std::uint32_t a_flags, void* a_dataOut);
__pragma(comment(linker, "/alternatename:__imp_W32_IMPL_CryptProtectData=__imp_CryptProtectData"))
extern "C" __declspec(dllimport) int __stdcall W32_IMPL_CryptUnprotectData(void* a_dataIn, wchar_t** a_description, void* a_entropy, void* a_reserved, void* a_prompt, std::uint32_t a_flags, void* a_dataOut);
__pragma(comment(linker, "/alternatename:__imp_W32_IMPL_CryptUnprotectData=__imp_CryptUnprotectData"))
extern "C" __declspec(dllimport) int __stdcall W32_IMPL_CryptBinaryToStringA(const std::uint8_t* a_binary, std::uint32_t a_binarySize, std::uint32_t a_flags, char* a_string, std::uint32_t* a_stringSize);
__pragma(comment(linker, "/alternatename:__imp_W32_IMPL_CryptBinaryToStringA=__imp_CryptBinaryToStringA"))
extern "C" __declspec(dllimport) int __stdcall W32_IMPL_CryptStringToBinaryA(const char* a_string, std::uint32_t a_stringSize, std::uint32_t a_flags, std::uint8_t* a_binary, std::uint32_t* a_binarySize, std::uint32_t* a_skip, std::uint32_t* a_flagsOut);
__pragma(comment(linker, "/alternatename:__imp_W32_IMPL_CryptStringToBinaryA=__imp_CryptStringToBinaryA"))
extern "C" __declspec(dllimport) void* __stdcall W32_IMPL_LocalFree(void* a_memory);
__pragma(comment(linker, "/alternatename:__imp_W32_IMPL_LocalFree=__imp_LocalFree"))
extern "C" __declspec(dllimport) std::uint32_t __stdcall W32_IMPL_GetLastError();
__pragma(comment(linker, "/alternatename:__imp_W32_IMPL_GetLastError=__imp_GetLastError"))

namespace F4T::ResumeTokenProtection
{
	namespace
	{
		struct DataBlob
		{
			std::uint32_t cbData{};
			std::uint8_t* pbData{};
		};

		constexpr std::uint32_t kCryptProtectUiForbidden = 0x1;
		constexpr std::uint32_t kCryptStringBase64 = 0x1;
		constexpr std::uint32_t kCryptStringNoCrlf = 0x40000000;

		thread_local std::uint32_t g_lastError{};

		std::optional<std::string> Fail()
		{
			g_lastError = W32_IMPL_GetLastError();
			return std::nullopt;
		}
	}

	std::optional<std::string> Protect(const std::string& a_plaintext)
	{
		DataBlob input{ static_cast<std::uint32_t>(a_plaintext.size()), reinterpret_cast<std::uint8_t*>(const_cast<char*>(a_plaintext.data())) };
		DataBlob output{};
		if (W32_IMPL_CryptProtectData(&input, nullptr, nullptr, nullptr, nullptr, kCryptProtectUiForbidden, &output) == 0) {
			return Fail();
		}
		std::unique_ptr<std::uint8_t, decltype(&W32_IMPL_LocalFree)> guard(output.pbData, &W32_IMPL_LocalFree);

		constexpr auto flags = kCryptStringBase64 | kCryptStringNoCrlf;
		std::uint32_t encodedLength = 0;
		if (W32_IMPL_CryptBinaryToStringA(output.pbData, output.cbData, flags, nullptr, &encodedLength) == 0) {
			return Fail();
		}
		std::vector<char> encoded(encodedLength);
		if (W32_IMPL_CryptBinaryToStringA(output.pbData, output.cbData, flags, encoded.data(), &encodedLength) == 0) {
			return Fail();
		}
		return std::string(encoded.data());
	}

	std::optional<std::string> Unprotect(const std::string& a_protectedBase64)
	{
		const auto inputLength = static_cast<std::uint32_t>(a_protectedBase64.size());
		std::uint32_t decodedLength = 0;
		if (W32_IMPL_CryptStringToBinaryA(a_protectedBase64.data(), inputLength, kCryptStringBase64, nullptr, &decodedLength, nullptr, nullptr) == 0) {
			return Fail();
		}
		std::vector<std::uint8_t> decoded(decodedLength);
		if (W32_IMPL_CryptStringToBinaryA(a_protectedBase64.data(), inputLength, kCryptStringBase64, decoded.data(), &decodedLength, nullptr, nullptr) == 0) {
			return Fail();
		}

		DataBlob input{ decodedLength, decoded.data() };
		DataBlob output{};
		if (W32_IMPL_CryptUnprotectData(&input, nullptr, nullptr, nullptr, nullptr, kCryptProtectUiForbidden, &output) == 0) {
			return Fail();
		}
		std::unique_ptr<std::uint8_t, decltype(&W32_IMPL_LocalFree)> guard(output.pbData, &W32_IMPL_LocalFree);
		return std::string(reinterpret_cast<const char*>(output.pbData), output.cbData);
	}

	std::uint32_t LastError() noexcept
	{
		return g_lastError;
	}
}

#else

namespace F4T::ResumeTokenProtection
{
	std::optional<std::string> Protect(const std::string&)
	{
		return std::nullopt;
	}

	std::optional<std::string> Unprotect(const std::string&)
	{
		return std::nullopt;
	}

	std::uint32_t LastError() noexcept
	{
		return 0;
	}
}

#endif
