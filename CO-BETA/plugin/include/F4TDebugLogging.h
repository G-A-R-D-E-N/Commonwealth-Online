#pragma once

#include <cstddef>
#include <format>
#include <string>
#include <string_view>
#include <utility>

namespace RE::BSScript
{
	class IVirtualMachine;
}

namespace F4T::DebugLogging
{
	void Initialize();
	void SetEnabled(bool a_enabled);
	bool IsEnabled();
	std::string SanitizeLogText(std::string_view a_text, std::size_t a_maxLength = 160);
	void Write(std::string_view a_component, std::string_view a_message);
	bool RegisterPapyrus(RE::BSScript::IVirtualMachine* a_vm);

	template <class... Args>
	void Log(std::string_view a_component, std::format_string<Args...> a_format, Args&&... a_args)
	{
		if (!IsEnabled()) {
			return;
		}
		Write(a_component, std::format(a_format, std::forward<Args>(a_args)...));
	}
}
