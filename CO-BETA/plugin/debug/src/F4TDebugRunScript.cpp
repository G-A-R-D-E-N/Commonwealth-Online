#include <F4SE/F4SE.h>
#include <RE/B/BSFixedString.h>
#include <RE/B/BSScriptUtil.h>

#include "CommonwealthOnlineUI_API.h"
#include "PrismaUI_F4_API.h"

#include <cstddef>
#include <string>
#include <variant>

namespace
{
	constexpr std::size_t kMaxScriptBytes = 4096;

	bool PapyrusRunScript(
		std::monostate,
		RE::BSFixedString a_view,
		RE::BSFixedString a_script)
	{
		const std::string view = a_view.c_str() ? a_view.c_str() : "";
		const std::string script = a_script.c_str() ? a_script.c_str() : "";
		if (script.empty() || script.size() > kMaxScriptBytes) {
			return false;
		}

		auto* ui = COMMONWEALTH_ONLINE_UI_API::RequestPluginAPI<COMMONWEALTH_ONLINE_UI_API::IVCommonwealthOnlineUI22>();
		auto* prisma = PRISMA_UI_API::RequestPluginAPI<PRISMA_UI_API::IVPrismaUI4>();
		if (!ui || !ui->GetViewHandle || !prisma) {
			return false;
		}

		const PrismaView handle = ui->GetViewHandle(view.c_str());
		if (handle == 0 || !prisma->IsValid(handle)) {
			return false;
		}
		prisma->Invoke(handle, script.c_str());
		return true;
	}

	bool RegisterPapyrus(RE::BSScript::IVirtualMachine* a_vm)
	{
		if (!a_vm) {
			return false;
		}
		a_vm->BindNativeMethod("CommonwealthOnlineDebugRun", "RunScript", PapyrusRunScript, true, false);
		return true;
	}
}

F4SE_PLUGIN_LOAD(const F4SE::LoadInterface* a_f4se)
{
	F4SE::Init(a_f4se);

	const auto* papyrus = F4SE::GetPapyrusInterface();
	if (!papyrus || !papyrus->Register(RegisterPapyrus)) {
		return false;
	}
	return true;
}
