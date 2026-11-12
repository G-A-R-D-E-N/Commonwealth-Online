#include "pch.h"
#include "F4TDeathBagLocal.h"
#include "F4TDeathBagView.h"
#include "RE/T/TESDataHandler.h"
#include "RE/T/TESObjectCELL.h"
#include "RE/T/TESObjectREFR.h"
#include <vector>

namespace F4T::DeathBagView
{
	void SweepCellSavedContainers(RE::TESObjectCELL& a_cell)
	{
		std::vector<RE::TESObjectREFR*> saved;
		a_cell.ForEachReference([&saved](RE::TESObjectREFR* a_ref) {
			if (a_ref != nullptr && F4T::DeathBagLocal::IsBagReference(*a_ref) && !IsViewReference(a_ref)) {
				saved.push_back(a_ref);
			}
			return RE::BSContainer::ForEachResult::kContinue;
		});
		for (auto* ref : saved) {
			ref->Disable();
			ref->MarkAsDeleted();
		}
	}

	void SweepSavedContainers()
	{
		auto* dataHandler = RE::TESDataHandler::GetSingleton();
		if (dataHandler == nullptr) {
			return;
		}
		for (auto* cell : dataHandler->GetFormArray<RE::TESObjectCELL>()) {
			if (cell != nullptr && cell->IsAttached()) {
				SweepCellSavedContainers(*cell);
			}
		}
	}
}
