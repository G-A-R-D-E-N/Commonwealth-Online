#include "pch.h"

#include "F4TDebugLogging.h"
#include "F4TDebugSwitch.h"

#include "RE/B/BSFixedString.h"
#include "RE/B/BSScriptUtil.h"

#include <algorithm>
#include <variant>

namespace
{
	bool PapyrusIsEnabled(std::monostate)
	{
		return F4T::DebugLogging::IsEnabled();
	}

	void PapyrusSetEnabled(std::monostate, bool a_enabled)
	{
		F4T::DebugLogging::SetEnabled(a_enabled);
	}

	void PapyrusTrace(
		std::monostate,
		RE::BSFixedString a_component,
		RE::BSFixedString a_message)
	{
		F4T::DebugLogging::Write(
			a_component.c_str() ? a_component.c_str() : "Papyrus",
			a_message.c_str() ? a_message.c_str() : "");
	}
}

namespace F4T::DebugLogging
{
	void Initialize()
	{
		DebugSwitch::InitializeFromEnvironment();
		REX::INFO(
			"[CO DEBUG] beta debug logging {} via {} (beta default={}).",
			IsEnabled() ? "enabled" : "disabled",
			DebugSwitch::EnvironmentVariable,
			DebugSwitch::BetaDefaultEnabled);
	}

	void SetEnabled(bool a_enabled)
	{
		DebugSwitch::SetEnabled(a_enabled);
		REX::INFO("[CO DEBUG] runtime debug logging {}.", a_enabled ? "enabled" : "disabled");
	}

	bool IsEnabled()
	{
		return DebugSwitch::IsEnabled();
	}

	std::string SanitizeLogText(std::string_view a_text, std::size_t a_maxLength)
	{
		std::string sanitized;
		sanitized.reserve((std::min)(a_text.size(), a_maxLength) + 3);
		for (std::size_t index = 0; index < a_text.size() && index < a_maxLength; ++index) {
			const auto value = static_cast<unsigned char>(a_text[index]);
			sanitized.push_back(value >= 0x20 && value != 0x7F ? static_cast<char>(value) : ' ');
		}
		if (a_text.size() > a_maxLength) {
			sanitized += "...";
		}
		return sanitized;
	}

	void Write(std::string_view a_component, std::string_view a_message)
	{
		if (!IsEnabled()) {
			return;
		}
		REX::INFO("[CO DEBUG][{}] {}", a_component, a_message);
	}

	bool RegisterPapyrus(RE::BSScript::IVirtualMachine* a_vm)
	{
		if (!a_vm) {
			return false;
		}
		a_vm->BindNativeMethod("CommonwealthOnlineDebug", "IsEnabled", PapyrusIsEnabled, true, false);
		a_vm->BindNativeMethod("CommonwealthOnlineDebug", "SetEnabled", PapyrusSetEnabled, true, false);
		a_vm->BindNativeMethod("CommonwealthOnlineDebug", "Trace", PapyrusTrace, true, false);
		return true;
	}
}
