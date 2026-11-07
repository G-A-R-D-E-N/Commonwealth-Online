#include "pch.h"

#include "F4TFailClosedNotice.h"

#include "RE/S/SendHUDMessage.h"

#include <string>

namespace F4T::FailClosedNotice
{
	void Show(std::string_view a_text)
	{
		const auto* taskInterface = F4SE::GetTaskInterface();
		if (!taskInterface) {
			return;
		}
		taskInterface->AddTask([text = std::string(a_text)]() {
			RE::SendHUDMessage::ShowHUDMessage(text.c_str(), nullptr, true, true);
		});
	}
}
