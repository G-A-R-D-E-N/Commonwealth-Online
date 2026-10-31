// Copyright (C) 2026 FatherLamb. Portions derived from FathersFallout, GPL-3.0 with additional terms. Used with the author's permission.
#include "F4TCrashReport.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

#define CHECK(expr)                                                                         \
	do {                                                                                    \
		if (!(expr)) {                                                                      \
			std::fprintf(stderr, "CHECK failed at line %d: %s\n", __LINE__, #expr);         \
			std::abort();                                                                   \
		}                                                                                   \
	} while (false)

namespace
{
	bool Contains(const char* a_text, const char* a_needle)
	{
		return std::strstr(a_text, a_needle) != nullptr;
	}

	void CopyModule(const char* a_name, F4T::CrashReport::Location& a_location)
	{
		const auto length = std::strlen(a_name);
		const auto copied = length < F4T::CrashReport::kMaxModuleName - 1 ? length : F4T::CrashReport::kMaxModuleName - 1;
		std::memcpy(a_location.module, a_name, copied);
		a_location.module[copied] = '\0';
	}
}

int main()
{
	using namespace F4T::CrashReport;

	char text[8192]{};

	Data data{};
	data.year = 2026;
	data.month = 10;
	data.day = 19;
	data.hour = 16;
	data.minute = 37;
	data.second = 2;
	data.millisecond = 5;
	data.exceptionCode = 0xC0000005;
	CopyModule("Fallout4.exe", data.faulting);
	data.faulting.offset = 0x1A2B3C;
	data.threadId = 4242;
	data.mainThread = true;
	data.accessViolation = true;
	data.accessType = 1;
	data.accessAddress = 0xDEADBEEF;
	for (std::size_t index = 0; index < kRegisterCount; ++index) {
		data.registers[index] = index + 1;
	}
	CopyModule("Fallout4.exe", data.frames[0]);
	data.frames[0].offset = 0x1000;
	CopyModule("CommonwealthOnline.dll", data.frames[1]);
	data.frames[1].offset = 0x20;
	data.frameCount = 2;

	const auto length = Format(data, text, sizeof(text));
	CHECK(length > 0);
	CHECK(length == std::strlen(text));
	CHECK(Contains(text, "[CO CRASH] ===== game crash report =====\r\n"));
	CHECK(Contains(text, "time=2026-10-19 16:37:02.005\r\n"));
	CHECK(Contains(text, "exception=0xC0000005 fault=Fallout4.exe+0x1A2B3C\r\n"));
	CHECK(Contains(text, "thread=4242 main-thread=yes\r\n"));
	CHECK(Contains(text, "access=write address=0x00000000DEADBEEF\r\n"));
	CHECK(Contains(text, "rax=0x0000000000000001 rbx=0x0000000000000002 rcx=0x0000000000000003 rdx=0x0000000000000004\r\n"));
	CHECK(Contains(text, "rsp=0x0000000000000008\r\n"));
	CHECK(Contains(text, "r8=0x0000000000000009 r9=0x000000000000000A r10=0x000000000000000B r11=0x000000000000000C\r\n"));
	CHECK(Contains(text, "r15=0x0000000000000010\r\n"));
	CHECK(Contains(text, "frame 00 Fallout4.exe+0x1000\r\n"));
	CHECK(Contains(text, "frame 01 CommonwealthOnline.dll+0x20\r\n"));
	CHECK(Contains(text, "[CO CRASH] ===== end crash report =====\r\n"));

	data.accessType = 8;
	Format(data, text, sizeof(text));
	CHECK(Contains(text, "access=execute address=0x00000000DEADBEEF\r\n"));

	data.accessType = 0;
	Format(data, text, sizeof(text));
	CHECK(Contains(text, "access=read address=0x00000000DEADBEEF\r\n"));

	data.accessViolation = false;
	Format(data, text, sizeof(text));
	CHECK(!Contains(text, "access="));

	data.mainThread = false;
	Format(data, text, sizeof(text));
	CHECK(Contains(text, "main-thread=no\r\n"));

	data.faulting = Location{};
	data.faulting.address = 0x7FF612345678ULL;
	Format(data, text, sizeof(text));
	CHECK(Contains(text, "fault=0x00007FF612345678\r\n"));

	data.frameCount = 0;
	Format(data, text, sizeof(text));
	CHECK(Contains(text, "frames=unavailable\r\n"));

	data.frameCount = kMaxFrames + 5;
	Format(data, text, sizeof(text));
	CHECK(Contains(text, "frame 31 "));
	CHECK(!Contains(text, "frame 32 "));

	Location full{};
	std::memset(full.module, 'M', sizeof(full.module));
	data.frames[0] = full;
	data.frameCount = 1;
	Format(data, text, sizeof(text));
	CHECK(Contains(text, "MMMM"));
	CHECK(!Contains(text, "frame 32 "));

	char small[32]{};
	const auto smallLength = Format(data, small, sizeof(small));
	CHECK(smallLength == sizeof(small) - 1);
	CHECK(small[smallLength] == '\0');

	CHECK(Format(data, nullptr, sizeof(text)) == 0);
	CHECK(Format(data, text, 0) == 0);

	F4T::CrashReport::Location fault{};
	CopyModule("Fallout4.exe", fault);
	fault.offset = 0x1A2B3C;
	const auto firstLength = FormatFirstChance(0xC0000005, fault, text, sizeof(text));
	CHECK(firstLength == std::strlen(text));
	CHECK(Contains(text, "[CO CRASH] first-chance exception=0xC0000005 fault=Fallout4.exe+0x1A2B3C (the game may still handle it)\r\n"));

	fault = Location{};
	fault.address = 0x7FF612345678ULL;
	FormatFirstChance(0xC0000374, fault, text, sizeof(text));
	CHECK(Contains(text, "first-chance exception=0xC0000374 fault=0x00007FF612345678 (the game may still handle it)\r\n"));

	char tiny[24]{};
	const auto tinyLength = FormatFirstChance(0xC0000005, fault, tiny, sizeof(tiny));
	CHECK(tinyLength == sizeof(tiny) - 1);
	CHECK(tiny[tinyLength] == '\0');
	CHECK(FormatFirstChance(0xC0000005, fault, nullptr, sizeof(text)) == 0);
	CHECK(FormatFirstChance(0xC0000005, fault, text, 0) == 0);

	return 0;
}
