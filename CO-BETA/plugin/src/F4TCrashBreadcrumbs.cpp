#include "pch.h"

#include "F4TCrashBreadcrumbs.h"

#include <Windows.h>

#include <array>

namespace
{
	F4T::CrashBreadcrumbs::Ring g_ring;
}

namespace F4T::CrashBreadcrumbs
{
	void Record(Code a_code, std::uint32_t a_formId, std::uint32_t a_aux)
	{
		g_ring.Record(GetTickCount64(), GetCurrentThreadId(), a_code, a_formId, a_aux);
	}

	std::size_t FormatDump(std::span<char> a_out)
	{
		std::array<Event, Capacity> events{};
		const auto count = g_ring.Snapshot(events);

		LineWriter header{ a_out };
		header.Text("# CO breadcrumbs total=");
		header.Decimal(g_ring.Total());
		header.Text(" shown=");
		header.Decimal(count);
		header.Text("\n");
		std::size_t used = header.Size();

		for (std::size_t index = 0; index < count && used < a_out.size(); ++index) {
			used += FormatLine(events[index], a_out.subspan(used));
		}
		return used;
	}
}
