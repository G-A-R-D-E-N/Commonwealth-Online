#include "F4TInlineHook.h"

#include <array>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>

namespace
{
	using F4T::InlineHookGuard::InstallVerdict;
	using F4T::InlineHook::Request;
	using F4T::VtableGuard::AddressRange;

	std::string g_lastLog;
	F4T::InlineHook::Result g_installed{};
	void* g_published{ nullptr };
	int g_publishCount{ 0 };
	bool g_publishedBeforeEnable{ false };
	std::uintptr_t g_publishTarget{ 0 };
	std::array<std::uint8_t, 16> g_publishPrologue{};

	void CaptureLog(std::string_view a_message)
	{
		g_lastLog.assign(a_message);
	}

	void ThrowingLog(std::string_view)
	{
		throw 1;
	}

	void RecordPublish(void* a_original)
	{
		++g_publishCount;
		g_published = a_original;
		if (a_original) {
			g_publishedBeforeEnable = std::memcmp(
				reinterpret_cast<const void*>(g_publishTarget), g_publishPrologue.data(), g_publishPrologue.size()) == 0;
		}
	}

	SAFETYHOOK_NOINLINE int Target(int a_value)
	{
		volatile int value = a_value;
		value = value * 3 + 7;
		value = value ^ 0x5A;
		value = value - 11;
		return value;
	}

	int Detour(int a_value)
	{
		return g_installed.hook->call<int>(a_value) + 1000;
	}
}

int main()
{
	const auto target = reinterpret_cast<std::uintptr_t>(&Target);
	const AddressRange codeSection{ target, target + 64 };

	std::array<std::uint8_t, 16> prologue{};
	std::memcpy(prologue.data(), reinterpret_cast<const void*>(target), prologue.size());
	const auto originalValue = Target(2);
	const auto destination = reinterpret_cast<void*>(&Detour);

	{
		auto expected = prologue;
		expected[0] ^= 0xFF;
		F4T::InlineHook::Result result{};
		F4T::InlineHook::Install(Request{ target, codeSection, expected, destination }, CaptureLog, result);
		assert(result.verdict == InstallVerdict::kPrologueMismatch);
		assert(!result.hook);
		assert(g_lastLog.find("differ from the expected prologue") != std::string::npos);
		assert(std::memcmp(reinterpret_cast<const void*>(target), prologue.data(), prologue.size()) == 0);
		assert(Target(2) == originalValue);
	}

	{
		alignas(16) std::array<std::uint8_t, 32> fake{};
		fake[0] = 0xE9;
		const auto fakeAddress = reinterpret_cast<std::uintptr_t>(fake.data());
		F4T::InlineHook::Result result{};
		F4T::InlineHook::Install(
			Request{ fakeAddress, AddressRange{ fakeAddress, fakeAddress + fake.size() }, prologue, destination },
			CaptureLog,
			result);
		assert(result.verdict == InstallVerdict::kPriorRel32Jump);
		assert(!result.hook);
		assert(fake[0] == 0xE9);
		assert(g_lastLog.find("rel32 jmp") != std::string::npos);
	}

	{
		F4T::InlineHook::Result result{};
		F4T::InlineHook::Install(Request{ target, codeSection, prologue, nullptr }, nullptr, result);
		assert(result.verdict == InstallVerdict::kDestinationUnresolved);
		assert(!result.hook);
		assert(std::memcmp(reinterpret_cast<const void*>(target), prologue.data(), prologue.size()) == 0);
	}

	{
		F4T::InlineHook::Result result{};
		F4T::InlineHook::Install(Request{ 0, codeSection, prologue, destination }, ThrowingLog, result);
		assert(result.verdict == InstallVerdict::kTargetUnresolved);
	}

	{
		F4T::InlineHook::Result result{};
		F4T::InlineHook::Install(Request{ 0, codeSection, prologue, destination }, nullptr, result);
		assert(result.verdict == InstallVerdict::kTargetUnresolved);
		assert(!result.hook);
	}

	{
		F4T::InlineHook::Result result{};
		F4T::InlineHook::Install(
			Request{ target, AddressRange{ 0x1000, 0x2000 }, prologue, destination }, nullptr, result);
		assert(result.verdict == InstallVerdict::kTargetNotExecutable);
		assert(!result.hook);
	}

	g_publishTarget = target;
	g_publishPrologue = prologue;

	{
		auto expected = prologue;
		expected[0] ^= 0xFF;
		F4T::InlineHook::Result result{};
		g_published = destination;
		F4T::InlineHook::Install(Request{ target, codeSection, expected, destination, RecordPublish }, nullptr, result);
		assert(result.verdict == InstallVerdict::kPrologueMismatch);
		assert(g_publishCount == 1);
		assert(g_published == nullptr);
	}

	g_publishCount = 0;
	F4T::InlineHook::Install(Request{ target, codeSection, prologue, destination, RecordPublish }, CaptureLog, g_installed);
	assert(g_installed.verdict == InstallVerdict::kOk);
	assert(g_installed.hook);
	assert(g_publishCount == 1);
	assert(g_publishedBeforeEnable);
	assert(g_published != nullptr);
	assert(g_published == g_installed.hook->original<void*>());
	assert(g_installed.hook->destination_address() == reinterpret_cast<std::uintptr_t>(destination));
	assert(g_lastLog.find("installed and verified") != std::string::npos);
	assert(Target(2) == originalValue + 1000);

	{
		auto expected = prologue;
		expected[0] ^= 0xFF;
		F4T::InlineHook::Install(Request{ target, codeSection, expected, destination }, nullptr, g_installed);
		assert(g_installed.verdict == InstallVerdict::kPrologueMismatch);
		assert(!g_installed.hook);
		assert(std::memcmp(reinterpret_cast<const void*>(target), prologue.data(), prologue.size()) == 0);
		assert(Target(2) == originalValue);
	}
	return 0;
}
