#include "pch.h"

#include "F4TPolicyNoInteract.h"

#include "F4TProxyNoTalk.h"

#include "RE/T/TESDataHandler.h"
#include "RE/T/TESForm.h"
#include "RE/T/TESNPC.h"

#include <cstdint>
#include <string_view>
#include <vector>

namespace F4T::PolicyNoInteract
{
	void Apply(const std::vector<F4T::WorldPolicy::FormKey>& a_keys)
	{
		auto* dataHandler = RE::TESDataHandler::GetSingleton();
		if (dataHandler == nullptr) {
			REX::WARN(std::string_view{ "policy NoInteract not applied: no data handler; the previous block set stays (TODO-verify(#669))" });
			return;
		}

		std::vector<std::uint32_t> blocked;
		RE::TESNPC* install = nullptr;
		for (const auto& key : a_keys) {
			const auto resolvedId = dataHandler->LookupFormID(key.localId, key.plugin);
			if (resolvedId == 0) {
				continue;
			}
			auto* npc = RE::TESForm::GetFormByID<RE::TESNPC>(resolvedId);
			if (npc == nullptr || npc->IsPlayer()) {
				continue;
			}
			blocked.push_back(resolvedId);
			if (install == nullptr) {
				install = npc;
			}
		}

		F4T::ProxyNoTalk::SetPolicyBlock(blocked);
		if (install != nullptr) {
			F4T::ProxyNoTalk::InstallOnBase(install);
		}
		if (!blocked.empty() && !F4T::ProxyNoTalk::Installed()) {
			REX::WARN(std::string_view{ "policy NPCs stay interactable: NoTalk is not installed (TODO-verify(#669))" });
		}
	}

	void Clear()
	{
		F4T::ProxyNoTalk::SetPolicyBlock({});
	}

	bool Active()
	{
		return F4T::ProxyNoTalk::PolicyBlockActive();
	}
}
