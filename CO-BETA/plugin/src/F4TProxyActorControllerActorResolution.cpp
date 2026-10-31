// source:CO-BETA/plugin/src/F4TProxyActorController.cpp moved verbatim (RM-10)
#include "pch.h"

#include "F4TProxyActorControllerInternal.h"

using namespace F4T::ProxyActorController::detail;

namespace
{
	constexpr RE::TESFormID kProxyBaseActorLocalFormId = 0x002666;
	constexpr RE::TESFormID kFemaleProxyBaseActorLocalFormId = 0x0091E5;
	constexpr std::uint32_t kDebugCodsworthRefFormId = 0x0001CA7D;
	constexpr auto kCkPlacedProxyEditorIdPrefix = "COProxyRemotePlayer";

	struct RuntimeActorBaseSelection
	{
		RE::TESNPC* baseActor{ nullptr };
		RuntimeActorBaseSource source{ RuntimeActorBaseSource::kCustomProxy };
	};

	struct AbsoluteFormLookupResult
	{
		RE::TESForm* form{ nullptr };
		RE::TESNPC* npc{ nullptr };
		RE::Actor* actor{ nullptr };
		RE::TESObjectREFR* reference{ nullptr };
	};
	bool g_proxyActorBaseDiagnosticStartedLogged = false;
	bool g_testPluginLoadedLogged = false;
	bool g_proxyActorBaseResolvedLogged = false;
	bool g_codsworthBaseAbsoluteLookupLogged = false;
	bool g_codsworthRefAbsoluteLookupLogged = false;
	bool g_codsworthBaseResolvedLogged = false;
	bool g_codsworthBaseLookupFailedLogged = false;
	bool g_codsworthRefResolvedLogged = false;

	bool HasEditorIdPrefix(const RE::TESForm& a_form, std::string_view a_prefix)
	{
		if (a_prefix.empty()) {
			return false;
		}

		const auto* editorId = a_form.GetFormEditorID();
		if (!editorId) {
			return false;
		}

		const auto id = std::string_view(editorId);
		return id.size() >= a_prefix.size() && id.substr(0, a_prefix.size()) == a_prefix;
	}

	bool IsTestPluginLoaded()
	{
		auto* dataHandler = RE::TESDataHandler::GetSingleton();
		if (!dataHandler) {
			LogThrottledWarning(
				"test_plugin_data_handler_missing",
				std::format(
					"Runtime proxy actor base lookup failed: TESDataHandler is unavailable; cannot check {} or resolve {}.",
					kTestPluginName,
					kProxyBaseActorEditorId));
			return false;
		}

		const auto* loadedMod = dataHandler->LookupLoadedModByName(kTestPluginName);
		const auto* mod = loadedMod ? loadedMod : dataHandler->LookupModByName(kTestPluginName);
		if (!mod) {
			LogThrottledWarning(
				"test_plugin_missing",
				std::format(
					"Runtime proxy actor base lookup failed: {} is not loaded; cannot resolve {}.",
					kTestPluginName,
					kProxyBaseActorEditorId));
			return false;
		}

		if (!g_testPluginLoadedLogged) {
			LogInfoWithLocalPlayerPrefix(std::format(
				"Runtime proxy actor base lookup confirmed plugin loaded: {}.",
				kTestPluginName));
			g_testPluginLoadedLogged = true;
		}

		return true;
	}

	bool IsUsableProxyActorBase(const RE::TESNPC& a_baseActor)
	{
		return a_baseActor.As<RE::TESBoundObject>() != nullptr;
	}

	std::string GetFullNameForLog(const RE::TESForm& a_form)
	{
		const auto fullName = RE::TESFullName::GetFullName(a_form);
		if (fullName.empty()) {
			return "unavailable";
		}

		return std::string(fullName);
	}

	AbsoluteFormLookupResult LookupAbsoluteFormForDiagnostic(
		std::uint32_t a_formId,
		std::string_view a_label,
		bool& a_logged)
	{
		AbsoluteFormLookupResult result;
		result.form = RE::TESForm::GetFormByID(a_formId);
		result.npc = result.form ? result.form->As<RE::TESNPC>() : nullptr;
		result.actor = result.form ? result.form->As<RE::Actor>() : nullptr;
		result.reference = result.form ? result.form->As<RE::TESObjectREFR>() : nullptr;

		if (!a_logged) {
			const auto resolvedFormId = result.form ? std::format("{:08X}", result.form->GetFormID()) : "unavailable";
			const auto* formType = result.form ? RE::TESForm::GetFormTypeString(result.form->GetFormType()) : nullptr;
			const auto editorId = result.form ? GetEditorIdForLog(*result.form) : "unavailable";
			const auto fullName = result.form ? GetFullNameForLog(*result.form) : "unavailable";
			LogInfoWithLocalPlayerPrefix(std::format(
				"Stage 3.4 absolute FormID lookup diagnostic: label={}, requestedFormId={:08X}, resolvedFormId={}, formType={}, editorId={}, displayName={}, resolved={}, castsToTESNPC={}, castsToActor={}, castsToTESObjectREFR={}.",
				a_label,
				a_formId,
				resolvedFormId,
				formType ? formType : "unavailable",
				editorId,
				fullName,
				result.form != nullptr,
				result.npc != nullptr,
				result.actor != nullptr,
				result.reference != nullptr));
			a_logged = true;
		}

		return result;
	}

	RE::Actor* ResolveCodsworthPlacedRefByAbsoluteFormId()
	{
		const auto result = LookupAbsoluteFormForDiagnostic(
			kDebugCodsworthRefFormId,
			"Codsworth placed ref",
			g_codsworthRefAbsoluteLookupLogged);

		if (!result.reference && !result.actor) {
			return nullptr;
		}

		auto* reference = result.reference ? result.reference : result.actor;
		if (!g_codsworthRefResolvedLogged) {
			const auto* parentCell = reference->GetParentCell();
			LogInfoWithLocalPlayerPrefix(std::format(
				"Stage 3.4 Codsworth placed ref resolved. ref={:08X}, actor={}, originalCell={}, originalPosition={}.",
				reference->GetFormID(),
				result.actor != nullptr,
				parentCell ? std::format("{:08X}", parentCell->GetFormID()) : "unavailable",
				FormatPosition(reference->GetPosition())));
			g_codsworthRefResolvedLogged = true;
		}

		if (!result.actor) {
			LogThrottledWarning(
				"stage34_codsworth_ref_not_actor",
				"Stage 3.4 Codsworth placed ref resolved as TESObjectREFR but not Actor; safe movement and IsVisible diagnostics are unavailable.");
			return nullptr;
		}

		return result.actor;
	}

	RE::TESNPC* ResolveProxyActorBaseByEditorId(const char* a_editorId)
	{
		auto* baseActor = RE::TESForm::GetFormByEditorID<RE::TESNPC>(RE::BSFixedString(a_editorId));
		if (baseActor && HasEditorId(*baseActor, a_editorId) && IsUsableProxyActorBase(*baseActor)) {
			if (!g_proxyActorBaseResolvedLogged) {
				LogInfoWithLocalPlayerPrefix(std::format(
					"Runtime proxy actor base resolved: {} by editor ID as form {:08X}.",
					a_editorId,
					baseActor->GetFormID()));
				g_proxyActorBaseResolvedLogged = true;
			}

			return baseActor;
		}

		auto* form = RE::TESForm::GetFormByEditorID(RE::BSFixedString(a_editorId));
		if (form) {
			LogThrottledWarning(
				std::string("proxy_actor_base_wrong_type_") + a_editorId,
				std::format(
					"Runtime proxy actor base editor ID lookup failed: {} resolved as form {:08X}, but it is not a usable TESNPC actor base; trying plugin-local FormID fallback.",
					a_editorId,
					form->GetFormID()));
		} else {
			LogThrottledWarning(
				std::string("proxy_actor_base_editor_id_missing_") + a_editorId,
				std::format(
					"Runtime proxy actor base editor ID lookup failed: {} could not be resolved; trying plugin-local FormID fallback.",
					a_editorId));
		}

		return nullptr;
	}

	RuntimeActorBaseSelection SelectRuntimeActorBase()
	{
		LogRuntimeActorBaseSourceIfNeeded(RuntimeActorBaseSource::kCustomProxy);
		return RuntimeActorBaseSelection{
			ResolveProxyActorBase(),
			RuntimeActorBaseSource::kCustomProxy
		};
	}
}

namespace F4T::ProxyActorController::detail
{
	RE::ActorHandle GetProxyActorHandle(RE::Actor& a_actor)
	{
		return RE::BSPointerHandleManagerInterface<RE::Actor>::GetHandle(std::addressof(a_actor));
	}

	RE::Actor* FindActorInProcessListsByFormId(std::uint32_t a_formId)
	{
		const auto* processLists = RE::ProcessLists::GetSingleton();
		if (!processLists) {
			return nullptr;
		}

		const auto tryHandleArray = [&](const RE::BSTArray<RE::ActorHandle>& a_handles) -> RE::Actor* {
			for (const auto& handle : a_handles) {
				if (const auto actorRef = handle.get()) {
					if (actorRef->GetFormID() == a_formId) {
						return actorRef.get();
					}
				}
			}
			return nullptr;
		};

		if (auto* actor = tryHandleArray(processLists->highActorHandles)) {
			return actor;
		}
		if (auto* actor = tryHandleArray(processLists->middleHighActorHandles)) {
			return actor;
		}
		if (auto* actor = tryHandleArray(processLists->middleLowActorHandles)) {
			return actor;
		}
		if (auto* actor = tryHandleArray(processLists->lowActorHandles)) {
			return actor;
		}

		return nullptr;
	}

	bool HasEditorId(const RE::TESForm& a_form, std::string_view a_editorId)
	{
		const auto* editorId = a_form.GetFormEditorID();
		return editorId && a_editorId == editorId;
	}

	bool IsCkPlacedProxyReference(
		const RE::TESObjectREFR& a_ref,
		const RE::Actor& a_actor,
		std::optional<RE::TESFormID> a_proxyBaseFormId)
	{
		if (HasEditorId(a_ref, kProxyEditorId) || HasEditorIdPrefix(a_ref, kCkPlacedProxyEditorIdPrefix)) {
			return true;
		}

		const auto refLocalFormId = a_ref.GetFormID() & 0x00FFFFFF;
		if (refLocalFormId == kProxyFallbackLocalFormId) {
			return true;
		}

		if (a_proxyBaseFormId) {
			const auto* baseActor = a_actor.GetNPC();
			if (baseActor && baseActor->GetFormID() == *a_proxyBaseFormId) {
				return true;
			}
		}

		return false;
	}

	std::string GetEditorIdForLog(const RE::TESForm& a_form)
	{
		const auto* editorId = a_form.GetFormEditorID();
		if (!editorId || std::string_view(editorId).empty()) {
			return "unavailable";
		}

		return editorId;
	}

	RE::TESNPC* ResolveCodsworthBaseByAbsoluteFormId()
	{
		const auto result = LookupAbsoluteFormForDiagnostic(
			kDebugCodsworthBaseFormId,
			"Codsworth base",
			g_codsworthBaseAbsoluteLookupLogged);

		if (result.npc) {
			if (!g_codsworthBaseResolvedLogged) {
				LogInfoWithLocalPlayerPrefix("Stage 3.4 Codsworth base resolved as TESNPC.");
				g_codsworthBaseResolvedLogged = true;
			}
			return result.npc;
		}

		if (!g_codsworthBaseLookupFailedLogged) {
			LogWarningWithLocalPlayerPrefix("Stage 3.4 Codsworth base lookup failed by absolute FormID.");
			g_codsworthBaseLookupFailedLogged = true;
		}
		return nullptr;
	}

	RE::TESNPC* ResolveProxyActorBaseByPluginLocalFormId(
		const char* a_editorId,
		RE::TESFormID a_localFormId)
	{
		auto* dataHandler = RE::TESDataHandler::GetSingleton();
		if (!dataHandler) {
			LogThrottledWarning(
				std::string("proxy_actor_base_form_id_data_handler_missing_") + a_editorId,
				std::format(
					"Runtime proxy actor base plugin-local FormID fallback failed: TESDataHandler is unavailable for {} local form {:06X} ({}).",
					kTestPluginName,
					a_localFormId,
					a_editorId));
			return nullptr;
		}

		auto* baseActor = dataHandler->LookupForm<RE::TESNPC>(a_localFormId, kTestPluginName);
		if (!baseActor) {
			LogThrottledWarning(
				std::string("proxy_actor_base_form_id_missing_") + a_editorId,
				std::format(
					"Runtime proxy actor base plugin-local FormID fallback failed: {} local form {:06X} ({}) did not resolve as a TESNPC.",
					kTestPluginName,
					a_localFormId,
					a_editorId));
			return nullptr;
		}

		if (!IsUsableProxyActorBase(*baseActor)) {
			LogThrottledWarning(
				std::string("proxy_actor_base_form_id_not_bound_object_") + a_editorId,
				std::format(
					"Runtime proxy actor base plugin-local FormID fallback failed: {} local form {:06X} ({}) is not usable as NEW_REFR_DATA::object.",
					kTestPluginName,
					a_localFormId,
					a_editorId));
			return nullptr;
		}

		const auto editorId = GetEditorIdForLog(*baseActor);
		const auto fullName = GetFullNameForLog(*baseActor);
		const auto* formType = RE::TESForm::GetFormTypeString(baseActor->GetFormType());
		const auto editorIdMatches = editorId == a_editorId;
		if (!editorIdMatches) {
			LogThrottledWarning(
				std::string("proxy_actor_base_form_id_editor_id_mismatch_accepted_") + a_editorId,
				std::format(
					"Runtime proxy actor base plugin-local FormID fallback editor ID check did not match: expected {}, got {}. Accepting confirmed {} local form {:06X} because it resolved as TESNPC form {:08X}.",
					a_editorId,
					editorId,
					kTestPluginName,
					a_localFormId,
					baseActor->GetFormID()));
		}

		if (!g_proxyActorBaseResolvedLogged) {
			LogInfoWithLocalPlayerPrefix(std::format(
				"Runtime proxy actor base resolved: {} by plugin-local FormID fallback. plugin={}, localFormId={:06X}, runtimeFormId={:08X}, formType={}, isTESNPC=true, editorId={}, displayName={}, accepted=true.",
				a_editorId,
				kTestPluginName,
				a_localFormId,
				baseActor->GetFormID(),
				formType ? formType : "unavailable",
				editorId,
				fullName));
			g_proxyActorBaseResolvedLogged = true;
		}

		return baseActor;
	}

	RE::TESNPC* ResolveProxyActorBaseByPluginLocalFormId()
	{
		return ResolveProxyActorBaseByPluginLocalFormId(kProxyBaseActorEditorId, kProxyBaseActorLocalFormId);
	}

	RE::TESNPC* ResolveFemaleProxyActorBaseByPluginLocalFormId()
	{
		return ResolveProxyActorBaseByPluginLocalFormId(
			kFemaleProxyBaseActorEditorId,
			kFemaleProxyBaseActorLocalFormId);
	}

	RE::TESNPC* ResolveProxyActorBaseForSex(bool a_wantFemale)
	{
		const auto* editorId = a_wantFemale ? kFemaleProxyBaseActorEditorId : kProxyBaseActorEditorId;
		const auto localFormId = a_wantFemale ? kFemaleProxyBaseActorLocalFormId : kProxyBaseActorLocalFormId;

		if (!g_proxyActorBaseDiagnosticStartedLogged) {
			LogInfoWithLocalPlayerPrefix(std::format(
				"Runtime proxy actor base lookup diagnostic running for {} / {}.",
				kProxyBaseActorEditorId,
				kFemaleProxyBaseActorEditorId));
			g_proxyActorBaseDiagnosticStartedLogged = true;
		}

		if (!IsTestPluginLoaded()) {
			return nullptr;
		}

		if (auto* baseActor = ResolveProxyActorBaseByEditorId(editorId)) {
			return baseActor;
		}

		auto* baseActor = ResolveProxyActorBaseByPluginLocalFormId(editorId, localFormId);
		if (baseActor) {
			return baseActor;
		}

		if (a_wantFemale) {
			LogThrottledWarning(
				"proxy_actor_base_female_missing_fallback_male",
				std::format(
					"Runtime female proxy actor base lookup failed for {}; falling back to male {}.",
					kFemaleProxyBaseActorEditorId,
					kProxyBaseActorEditorId));
			if (auto* maleBase = ResolveProxyActorBaseByEditorId(kProxyBaseActorEditorId)) {
				return maleBase;
			}
			return ResolveProxyActorBaseByPluginLocalFormId(
				kProxyBaseActorEditorId,
				kProxyBaseActorLocalFormId);
		}

		LogThrottledWarning(
			"proxy_actor_base_missing",
			std::format(
				"Runtime proxy actor base lookup failed: {} was not resolved by editor ID or {} plugin-local FormID {:06X}; runtime spawning remains disabled.",
				editorId,
				kTestPluginName,
				localFormId));
		return nullptr;
	}

	RE::TESNPC* ResolveProxyActorBase()
	{
		return ResolveProxyActorBaseForSex(false);
	}

	std::optional<bool> GetDesiredProxyIsFemale(const RemotePlayer& a_remotePlayer)
	{
		if (!a_remotePlayer.hasAppearanceUpdate || !a_remotePlayer.appearance.isFemale.has_value()) {
			return std::nullopt;
		}

		return *a_remotePlayer.appearance.isFemale;
	}

	bool IsCustomPlayerProxyBase(const RE::TESNPC& a_baseActor)
	{
		return HasEditorId(a_baseActor, kProxyBaseActorEditorId) ||
		       HasEditorId(a_baseActor, kFemaleProxyBaseActorEditorId);
	}

	bool IsCustomPlayerProxyActor(const RE::Actor& a_actor)
	{
		const auto* baseActor = a_actor.GetNPC();
		if (!baseActor) {
			return false;
		}

		if (IsCustomPlayerProxyBase(*baseActor)) {
			return true;
		}

		if (auto* maleBase = ResolveProxyActorBaseByPluginLocalFormId()) {
			if (baseActor->GetFormID() == maleBase->GetFormID()) {
				return true;
			}
		}
		if (auto* femaleBase = ResolveFemaleProxyActorBaseByPluginLocalFormId()) {
			if (baseActor->GetFormID() == femaleBase->GetFormID()) {
				return true;
			}
		}

		return false;
	}

	std::optional<std::uint32_t> ParseHexFormId(const std::string& a_formId)
	{
		if (a_formId.empty()) {
			return std::nullopt;
		}

		std::uint32_t formId = 0;
		const auto* begin = a_formId.data();
		const auto* end = begin + a_formId.size();
		const auto result = std::from_chars(begin, end, formId, 16);
		if (result.ec != std::errc{} || result.ptr != end) {
			return std::nullopt;
		}

		return formId;
	}

	std::optional<std::uint32_t> GetLocalWorldspaceId(
		const RE::PlayerCharacter& a_player,
		const RE::TESObjectCELL& a_parentCell)
	{
		if (a_player.cachedWorldspace) {
			return a_player.cachedWorldspace->GetFormID();
		}

		if (a_parentCell.IsExterior() && a_parentCell.worldSpace) {
			return a_parentCell.worldSpace->GetFormID();
		}

		return std::nullopt;
	}
}
