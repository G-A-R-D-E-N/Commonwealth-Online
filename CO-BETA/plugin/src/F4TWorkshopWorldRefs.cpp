#include "pch.h"
#include "F4TWorkshopWorldRefs.h"

#include <mutex>
#include <set>
#include <utility>

namespace F4T::WorkshopWorldRefs
{
	namespace
	{
		constexpr float kMatchDistanceSq{ 1.0F };
		std::mutex g_hiddenMutex;
		std::set<std::uint32_t> g_hidden;
		std::set<std::uint32_t> g_retiring;
	}

	void Hide(std::uint32_t a_formId, const Workshop::Transform& a_transform)
	{
		auto* ref = RE::TESForm::GetFormByID<RE::TESObjectREFR>(a_formId);
		if (!ref || ref->IsDeleted()) {
			return;
		}
		const auto position = ref->GetPosition();
		const float dx = position.x - a_transform.x;
		const float dy = position.y - a_transform.y;
		const float dz = position.z - a_transform.z;
		if (dx * dx + dy * dy + dz * dz > kMatchDistanceSq) {
			return;
		}
		if (ref->IsDisabled()) {
			std::scoped_lock lock(g_hiddenMutex);
			if (g_retiring.contains(a_formId)) {
				g_hidden.insert(a_formId);
			}
			return;
		}
		// TODO-verify: session-only Disable() hide of a placed ref unverified in game
		ref->Disable();
		std::scoped_lock lock(g_hiddenMutex);
		g_hidden.insert(a_formId);
	}

	std::set<std::uint32_t> Detach()
	{
		std::scoped_lock lock(g_hiddenMutex);
		g_retiring.insert(g_hidden.begin(), g_hidden.end());
		return std::exchange(g_hidden, {});
	}

	// Game thread only: calls into the engine.
	void Restore(const std::set<std::uint32_t>& a_formIds)
	{
		for (const auto formId : a_formIds) {
			{
				std::scoped_lock lock(g_hiddenMutex);
				g_retiring.erase(formId);
				if (g_hidden.contains(formId)) {
					continue;
				}
			}
			if (auto* ref = RE::TESForm::GetFormByID<RE::TESObjectREFR>(formId); ref && !ref->IsDeleted()) {
				// TODO-verify: Enable(false) restore of a hidden placed ref unverified in game
				ref->Enable(false);
			}
		}
	}
}
