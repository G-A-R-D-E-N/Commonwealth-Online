#pragma once

#include "F4TSessionSaveGuard.h"

#include <string_view>

namespace F4T::SessionSaveQueue
{
	bool StartQueuedSave(SessionSaveGuard::ArmOwner a_owner, std::string_view a_reason);
	void StopQueue(SessionSaveGuard::ArmOwner a_owner);
	void PumpQueue(SessionSaveGuard::ArmOwner a_owner);
	void NoteEntered(SessionSaveGuard::ArmOwner a_owner, bool a_result);
	bool TakeOutstanding();
	bool FirstJoinSaveAccepted();
}
