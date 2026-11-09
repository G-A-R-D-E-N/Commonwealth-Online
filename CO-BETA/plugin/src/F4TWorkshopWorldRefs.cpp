#include "pch.h"
#include "F4TWorkshopWorldRefs.h"

#include <set>

namespace F4T::WorkshopWorldRefs
{
	namespace
	{
		constexpr float kMatchDistanceSq{ 1.0F };
		std::set<std::uint32_t> g_hidden;
	}

	void Hide(std::uint32_t a_formId, const Workshop::Transform& a_transform)
	{
		auto* ref = RE::TESForm::GetFormByID<RE::TESObjectREFR>(a_formId);
		if (!ref || ref->IsDeleted() || ref->IsDisabled()) {
			return;
		}
		const auto position = ref->GetPosition();
		const float dx = position.x - a_transform.x;
		const float dy = position.y - a_transform.y;
		const float dz = position.z - a_transform.z;
		if (dx * dx + dy * dy + dz * dz > kMatchDistanceSq) {
			return;
		}
		// TODO-verify: session-only Disable() hide of a placed ref unverified in game
		ref->Disable();
		g_hidden.insert(a_formId);
	}

	void Restore()
	{
		for (const auto formId : g_hidden) {
			if (auto* ref = RE::TESForm::GetFormByID<RE::TESObjectREFR>(formId); ref && !ref->IsDeleted()) {
				// TODO-verify: Enable(false) restore of a hidden placed ref unverified in game
				ref->Enable(false);
			}
		}
		g_hidden.clear();
	}
}
