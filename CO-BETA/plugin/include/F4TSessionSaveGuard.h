#pragma once

#include <functional>
#include <string_view>

namespace RE
{
	class BGSSaveLoadManager;
}

namespace F4T::SessionSaveGuard
{
	enum class ArmOwner
	{
		kNone,
		kFirstJoin,
		kQuit
	};

	void ArmInternalSave(std::string_view a_reason, ArmOwner a_owner = ArmOwner::kFirstJoin);
	std::string_view OwnerName(ArmOwner a_owner);
	void DisarmInternalSave(std::string_view a_reason);
	void RunOrDeferSessionExit(std::string_view a_reason, std::function<void()> a_proceed);
	void NotePostSaveGame();
	bool IsSessionExitPending();
	bool IsSessionExitLanded();
	bool OriginalIsSavingAllowed(const RE::BGSSaveLoadManager* a_manager);
	void Install();
	void Tick();
}
