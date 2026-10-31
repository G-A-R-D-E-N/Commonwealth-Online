#include "pch.h"

#include "SessionCallbacks.h"
#include "F4TPrismaUI.h"
#include "F4TScaleformUI.h"

#include "Scaleform/G/GFx_FunctionHandler.h"
#include "Scaleform/G/GFx_Movie.h"
#include "Scaleform/G/GFx_Value.h"

namespace F4T::ScaleformUI
{
	namespace
	{
		class OpenManagerHandler :
			public Scaleform::GFx::FunctionHandler
		{
		public:
			void Call(const Params& a_params) override
			{
				(void)a_params;

				const auto* taskInterface = F4SE::GetTaskInterface();
				if (taskInterface) {
					taskInterface->AddTask([]() {
						F4T::PrismaUI::ShowServerBrowser();
					});
					return;
				}

				F4T::PrismaUI::ShowServerBrowser();
			}
		};

		class CloseManagerHandler :
			public Scaleform::GFx::FunctionHandler
		{
		public:
			void Call(const Params& a_params) override
			{
				(void)a_params;

				F4T::PrismaUI::HideServerBrowser();

				const auto* taskInterface = F4SE::GetTaskInterface();
				if (taskInterface) {
					taskInterface->AddTask([]() {
						F4T::PrismaUI::HideServerBrowser();
					});
				}
			}
		};

		OpenManagerHandler  g_openManagerHandler;
		CloseManagerHandler g_closeManagerHandler;

		bool F4SEAPI RegisterCommonwealthOnlineScaleform(
			Scaleform::GFx::Movie* a_view,
			Scaleform::GFx::Value* a_plugin)
		{
			if (!a_view || !a_plugin) {
				return false;
			}

			Scaleform::GFx::Value openManager;
			a_view->CreateFunction(&openManager, &g_openManagerHandler);
			if (!a_plugin->SetMember("openManager", openManager)) {
				return false;
			}

			Scaleform::GFx::Value closeManager;
			a_view->CreateFunction(&closeManager, &g_closeManagerHandler);
			return a_plugin->SetMember("closeManager", closeManager);
		}
	}

	void RegisterPlugin()
	{
		const auto* scaleform = F4SE::GetScaleformInterface();
		if (!scaleform) {
			REX::WARN("{} [Scaleform] Scaleform interface unavailable; Main Menu Multiplayer button will not open the server browser.",
				std::string{COUI::Session::GetLogPrefix()});
			return;
		}

		if (!scaleform->Register("commonwealthOnline", RegisterCommonwealthOnlineScaleform)) {
			REX::WARN("{} [Scaleform] Failed to register commonwealthOnline Scaleform API.",
				std::string{COUI::Session::GetLogPrefix()});
			return;
		}

		REX::INFO("{} [Scaleform] Registered root.f4se.plugins.commonwealthOnline openManager/closeManager.",
			std::string{COUI::Session::GetLogPrefix()});
	}
}
