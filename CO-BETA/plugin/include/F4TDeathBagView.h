#pragma once
#include "RE/B/BSPointerHandle.h"
#include <nlohmann/json.hpp>
#include <string>
namespace RE { class TESObjectCELL; class TESObjectREFR; }
namespace F4T::DeathBagView
{
	void NoteDiscovery(const nlohmann::json& a_bags);
	void Tick(double a_nowSeconds);
	void Reset();
	void Adopt(const std::string& a_deathId, RE::ObjectRefHandle a_bag, const nlohmann::json& a_stacks);
	void Void(const std::string& a_deathId, bool a_emptied = false);
	void SweepSavedContainers();
	void SweepCellSavedContainers(RE::TESObjectCELL& a_cell);
	[[nodiscard]] bool IsMirrored(const std::string& a_deathId);
	[[nodiscard]] bool IsViewReference(const RE::TESObjectREFR* a_reference);
	[[nodiscard]] bool SnapshotHold();
}
