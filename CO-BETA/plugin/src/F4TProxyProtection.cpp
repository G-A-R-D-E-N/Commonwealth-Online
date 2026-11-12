#include "pch.h"
// address source:commonlibf4-nomad

#include "F4TProxyProtection.h"

#include "F4TDebugLogging.h"
#include "F4TFailClosedNotice.h"
#include "F4TRuntimeSelect.h"
#include "F4TProxyPuppet.h"
#include "RE/E/ExtraDataList.h"
#include "RE/P/PlayerCharacter.h"

namespace F4T::ProxyProtection
{
	namespace
	{
// address source:RE-Work/subsystems/commonwealth-online-re/pickup-gate-co309/README.md (ExtraFlags setter, verified per runtime)
		inline constexpr REL::ID kSetExtraFlags{ 723287, 2190560, 2190560 };

		using SetExtraFlagsFn = void (*)(RE::ExtraDataList*, std::uint32_t, bool);

		bool IsVerifiedRuntime()
		{
			static const bool verified = [] {
				const auto version = REX::FModule::GetExecutingModule().GetFileVersion();
				if (version == F4T::RuntimeSelect::VerifiedVersion()) {
					return true;
				}
				REX::WARN(
					"Commonwealth Online proxy activation protection is disabled: runtime {} is not verified (expected {}).",
					version.string(),
					F4T::RuntimeSelect::VerifiedVersion().string());
				F4T::FailClosedNotice::Show("Commonwealth Online is not verified for this Fallout 4 version: proxy activation protection is off.");
				return false;
			}();
			return verified;
		}

		void LogMissingExtraListOnce()
		{
			static bool logged = false;
			if (logged) {
				return;
			}
			logged = true;
			REX::WARN("Commonwealth Online proxy activation protection: a runtime proxy had no extra data list; its activation could not be blocked.");
		}
	}

	bool ApplyToProxyActor(RE::Actor& a_proxy)
	{
		const auto formID = a_proxy.GetFormID();
		const auto* player = RE::PlayerCharacter::GetSingleton();
		const auto playerFormID = player ? player->GetFormID() : 0U;
		if (!ShouldBlockActivation(ProxyPuppet::IsProxyFormID(formID), formID, playerFormID)) {
			return false;
		}

		if (!IsVerifiedRuntime()) {
			return false;
		}

		auto* extraList = a_proxy.extraList.get();
		if (!extraList) {
			LogMissingExtraListOnce();
			return false;
		}

		if (a_proxy.IsActivationBlocked()) {
			return true;
		}

		static REL::Relocation<SetExtraFlagsFn> setExtraFlags{ kSetExtraFlags };
		setExtraFlags(extraList, kProxyActivationBlockExtraFlags, true);

		F4T::DebugLogging::Log(
			"ProxyProtection",
			"Runtime proxy actor {:08X} activation blocked for the local player (extraFlags {:#X}).",
			formID,
			kProxyActivationBlockExtraFlags);
		return true;
	}
}
