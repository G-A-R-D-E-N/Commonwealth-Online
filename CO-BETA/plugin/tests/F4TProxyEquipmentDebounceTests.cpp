// Copyright (C) 2026 FatherLamb. Portions derived from FathersFallout, GPL-3.0 with additional terms. Used with the author's permission.
#include "F4TProxyEquipmentDebounce.h"

#include <cstdio>
#include <cstdlib>

#define CHECK(expr)                                                                         \
	do {                                                                                    \
		if (!(expr)) {                                                                      \
			std::fprintf(stderr, "CHECK failed at line %d: %s\n", __LINE__, #expr);         \
			std::abort();                                                                   \
		}                                                                                   \
	} while (false)

int main()
{
	using F4T::ProxyEquipmentDebounce::ShouldApply;
	using F4T::ProxyEquipmentDebounce::State;
	using std::chrono::milliseconds;

	const auto t0 = F4T::ProxyEquipmentDebounce::Clock::time_point{} + std::chrono::seconds(100);

	State idle{ t0, t0 };
	CHECK(ShouldApply(idle, true, t0));
	CHECK(!ShouldApply(idle, false, t0 + milliseconds(349)));
	CHECK(ShouldApply(idle, false, t0 + milliseconds(350)) == false);

	State fresh{ t0, {} };
	CHECK(!ShouldApply(fresh, false, t0 + milliseconds(349)));
	CHECK(ShouldApply(fresh, false, t0 + milliseconds(350)));

	State gap{ t0 + milliseconds(1000), t0 + milliseconds(1300) };
	CHECK(!ShouldApply(gap, false, t0 + milliseconds(1350)));
	CHECK(!ShouldApply(gap, false, t0 + milliseconds(2199)));
	CHECK(ShouldApply(gap, false, t0 + milliseconds(2200)));

	CHECK(ShouldApply(gap, true, t0 + milliseconds(1000)));

	return 0;
}
