// License: GPL-3.0 with additional terms (FatherLamb); ported from FathersFallout client-ng/src/sync/BodyLights.cpp.
#include "pch.h"

#include "F4TPowerArmorSync.h"
#include "F4TProxyActorControllerInternal.h"
#include "F4TProxySaveSafety.h"
#include "RE/N/NEW_REFR_DATA.h"
#include "RE/N/NiUpdateData.h"
#include "RE/T/TESDataHandler.h"

#include <cmath>
#include <cstdint>

using namespace F4T::ProxyActorController::detail;

namespace F4T::ProxyActorController::detail
{
	namespace
	{
		constexpr std::uint32_t kPipboyLightBaseFormId = 0x0001ED2D;
		constexpr float kPipboyLightAhead = 30.0F;
		constexpr float kPipboyLightChestHeight = 95.0F;
		constexpr float kPipboyLightHelmetHeight = 150.0F;
		constexpr int kMaximumPipboyLightDepth = 6;

		RE::TESObjectREFR* FindPipboyLightRef(const ProxyActorSlot& a_slot)
		{
			if (a_slot.pipboyLightRefFormId == 0) {
				return nullptr;
			}
			auto* ref = RE::TESForm::GetFormByID<RE::TESObjectREFR>(a_slot.pipboyLightRefFormId);
			const auto* base = ref ? ref->GetObjectReference() : nullptr;
			return ref && !ref->IsDeleted() && base && base->GetFormID() == kPipboyLightBaseFormId ? ref : nullptr;
		}

		RE::NiPoint3 PipboyLightSpot(RE::Actor& a_proxy, bool a_armor)
		{
			auto spot = a_proxy.GetPosition();
			const float heading = a_proxy.GetHeading();
			spot.x += std::sin(heading) * kPipboyLightAhead;
			spot.y += std::cos(heading) * kPipboyLightAhead;
			spot.z += a_armor ? kPipboyLightHelmetHeight : kPipboyLightChestHeight;
			return spot;
		}

		RE::NiLight* FindPipboyLight(RE::NiAVObject* a_object, int a_depth = 0)
		{
			if (!a_object || a_depth > kMaximumPipboyLightDepth) {
				return nullptr;
			}
			if (auto* light = netimmerse_cast<RE::NiLight*>(a_object)) {
				return light;
			}
			if (auto* node = a_object->IsNode()) {
				for (auto& child : node->children) {
					if (auto* light = FindPipboyLight(child.get(), a_depth + 1)) {
						return light;
					}
				}
			}
			return nullptr;
		}

		void TintPipboyLight(ProxyActorSlot& a_slot, RE::TESObjectREFR& a_ref, std::uint32_t a_color)
		{
			auto* root = a_ref.Get3D();
			if (!root) {
				a_slot.pipboyLightTintedRoot = nullptr;
				a_slot.pipboyLightTintedColor = 0;
				return;
			}
			if (a_color == 0 || (a_slot.pipboyLightTintedRoot == root && a_slot.pipboyLightTintedColor == a_color)) {
				return;
			}
			auto* light = FindPipboyLight(root);
			if (!light) {
				return;
			}
			light->diff = { ((a_color >> 16) & 0xFF) / 255.0F, ((a_color >> 8) & 0xFF) / 255.0F, (a_color & 0xFF) / 255.0F };
			a_slot.pipboyLightTintedRoot = root;
			a_slot.pipboyLightTintedColor = a_color;
		}

		RE::TESObjectREFR* PlacePipboyLight(ProxyActorSlot& a_slot, RE::Actor& a_proxy, bool a_armor)
		{
			auto* base = RE::TESForm::GetFormByID<RE::TESBoundObject>(kPipboyLightBaseFormId);
			auto* dataHandler = RE::TESDataHandler::GetSingleton();
			auto* cell = a_proxy.GetParentCell();
			if (!base || !dataHandler || !cell) {
				return nullptr;
			}
			RE::NEW_REFR_DATA data;
			data.location = PipboyLightSpot(a_proxy, a_armor);
			data.direction = { 0.0F, 0.0F, a_proxy.GetHeading() };
			data.object = base;
			data.interior = cell->IsExterior() ? nullptr : cell;
			data.world = cell->IsExterior() ? cell->worldSpace : nullptr;
			auto handle = dataHandler->CreateReferenceAtLocation(data);
			const auto ref = handle.get();
			if (!ref) {
				return nullptr;
			}
			a_slot.pipboyLightRefFormId = ref->GetFormID();
			a_slot.pipboyLightCell = cell;
			a_slot.pipboyLightArmor = a_armor;
			return ref.get();
		}
	}

	void RemoveProxyPipboyLight(ProxyActorSlot& a_slot)
	{
		if (auto* ref = FindPipboyLightRef(a_slot)) {
			F4T::ProxySaveSafety::DestroyProxyActor(*ref);
		}
		a_slot.pipboyLightRefFormId = 0;
		a_slot.pipboyLightCell = nullptr;
		a_slot.pipboyLightArmor = false;
		a_slot.pipboyLightTintedRoot = nullptr;
		a_slot.pipboyLightTintedColor = 0;
	}

	void ApplyProxyPipboyLightFromRemoteState(
		ProxyActorSlot& a_slot,
		RE::Actor& a_proxy,
		const RemotePlayer& a_remotePlayer)
	{
		auto* cell = a_proxy.GetParentCell();
		if (!a_remotePlayer.hasPipboyLightUpdate || !a_remotePlayer.pipboyLightOn || !a_proxy.Get3D() || !cell) {
			RemoveProxyPipboyLight(a_slot);
			return;
		}

		const bool armor = F4T::PowerArmorSync::ProxyReportsInPowerArmor(a_proxy);
		auto* ref = FindPipboyLightRef(a_slot);
		if (!ref || a_slot.pipboyLightCell != cell || a_slot.pipboyLightArmor != armor) {
			RemoveProxyPipboyLight(a_slot);
			ref = PlacePipboyLight(a_slot, a_proxy, armor);
		}
		if (!ref) {
			return;
		}

		const auto spot = PipboyLightSpot(a_proxy, armor);
		ref->SetLocationOnReference(spot);
		ref->SetAngleOnReference(RE::NiPoint3{ 0.0F, 0.0F, a_proxy.GetHeading() });
		ref->Update3DPosition(true);
		if (auto* node = ref->Get3D()) {
			const auto delta = node->world.translate - spot;
			if (delta.x * delta.x + delta.y * delta.y + delta.z * delta.z > 1.0F) {
				node->local.translate = spot;
				RE::NiUpdateData updateData{};
				node->UpdateWorldData(&updateData);
			}
		}
		TintPipboyLight(a_slot, *ref, a_remotePlayer.pipboyLightColor);
	}
}
