#include "F4TPrismaUIInternal.h"
#include "F4TWorldMode.h"

#include <mutex>
#include <string>

namespace COUI::WorldMode
{
	namespace
	{
		std::mutex g_modeMutex;
		std::string g_mode;
	}

	std::string ToJson()
	{
		std::scoped_lock lock(g_modeMutex);
		return nlohmann::json{
			{ "worldMode", g_mode },
			{ "label", std::string{ Label(g_mode) } }
		}.dump();
	}

	void Push()
	{
		F4T::PrismaUI::Internal::DispatchPauseMenuWorldModeInternal(ToJson());
	}

	void Set(std::string_view a_mode)
	{
		{
			std::scoped_lock lock(g_modeMutex);
			g_mode = Normalize(a_mode);
		}
		Push();
	}
}
