#pragma once

#include "PrismaUI_F4_API.h"

#include <nlohmann/json_fwd.hpp>

namespace F4T::Settings
{
	class SettingsBridge
	{
	public:
		static void Initialize(PRISMA_UI_API::IVPrismaUI4* a_api);
		static void HandleAction(const nlohmann::json& a_action, PrismaView a_view);
	};
}
