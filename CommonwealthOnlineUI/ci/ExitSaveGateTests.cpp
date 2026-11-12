#include "F4TExitSaveGate.h"

#include <cassert>
#include <cstdint>

using F4T::ExitSave::Gate;
using F4T::ExitSave::kPostSaveGraceMs;
using F4T::ExitSave::kTimeoutMs;

int main()
{
	{
		Gate gate;
		gate.OnPostSave(0);
		assert(!gate.TakeDisconnect(kTimeoutMs * 2));
	}

	{
		Gate gate;
		const std::uint64_t t0 = 1000;
		gate.Arm(t0);
		assert(!gate.TakeDisconnect(t0 + kTimeoutMs - 1));
		assert(gate.TakeDisconnect(t0 + kTimeoutMs));
		assert(!gate.TakeDisconnect(t0 + kTimeoutMs));
	}

	{
		Gate gate;
		const std::uint64_t t = 5000;
		gate.Arm(0);
		gate.OnPostSave(t);
		assert(!gate.TakeDisconnect(t + kPostSaveGraceMs - 1));
		assert(gate.TakeDisconnect(t + kPostSaveGraceMs));
		assert(gate.PostSaveSeen());
		assert(!gate.TakeDisconnect(t + kPostSaveGraceMs));
	}

	{
		Gate gate;
		gate.OnPostSave(100);
		gate.Arm(200);
		assert(!gate.TakeDisconnect(200 + kTimeoutMs - 1));
		assert(gate.TakeDisconnect(200 + kTimeoutMs));
	}

	{
		Gate gate;
		gate.Arm(0);
		gate.OnPostSave(1000);
		gate.OnPostSave(5000);
		assert(!gate.TakeDisconnect(1000 + kPostSaveGraceMs - 1));
		assert(gate.TakeDisconnect(1000 + kPostSaveGraceMs));
	}

	{
		Gate gate;
		gate.Arm(0);
		assert(gate.TakeDisconnect(kTimeoutMs));
		gate.Arm(20000);
		assert(!gate.TakeDisconnect(20000 + kTimeoutMs - 1));
		assert(gate.TakeDisconnect(20000 + kTimeoutMs));
	}

	return 0;
}
