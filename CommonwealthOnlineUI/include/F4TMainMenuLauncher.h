#pragma once

#include "PrismaUI_F4_API.h"

#include <string_view>

namespace F4T::MainMenuLauncher
{
	void Initialize(PRISMA_UI_API::IVPrismaUI6* a_api);
	void EnsureView();
	void Reconcile(bool a_wanted, std::string_view a_reason);
	void Hide(std::string_view a_reason);
}
