#include "ServerAdminMenuDrain.h"
#include "AdminCommandDispatcher.h"
#include "AdminControlServer.h"
#include "AdminTokenStore.h"
#include "BanStore.h"
#include "DurableStateFileStore.h"
#include "IdentityGate.h"
#include "DirectoryRegistrar.h"
#include "LanDiscoveryService.h"
#include "OpenSslTokenService.h"
#include "PersistenceGate.h"
#include "SaveAttestationAuthority.h"
#include "SaveRecoveryAdmin.h"
#include "ServerConfiguration.h"
#include "ServerProtocol.h"
#include "ServerFactionAdminAction.h"
#include "ServerQuestAdminAction.h"
#include "ServerPacketDecode.h"
#include "ServerLiveRelay.h"
#include "ServerLiveSettings.h"
#include "F4TUtf8Path.h"
#include "F4TServerSettingsFile.h"
#include "ServerTextHelpers.h"
#include "SteamAdmin.h"
#include "SteamIdentityRegistry.h"
#include "SteamWebApiVerifier.h"
#include "ServerVoiceHost.h"
#include "ServerVoiceMeter.h"
#include "F4TAdmission.h"
#include "F4TConnectionPolicy.h"
#include "F4TPacketRateLimit.h"
#include "F4TMessageFragments.h"
#include "F4TPayloadCodec.h"
#include "F4TServerWorldState.h"
#include "co_gns_server_bridge.h"
#include "ServerBootstrap.h"
#include "ServerRuntimeSupport.h"
#include "ServerLiveSettingsDelivery.h"
#include "ServerGnsPollRetry.h"
#include "ServerDeferredRuntime.h"
#include "ServerEnforcement.h"
#include "ServerManifestChunks.h"
#include "ServerModpackFiles.h"
#include "ServerScriptHostBinding.h"
#include "ServerScriptResources.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <exception>
#include <iostream>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <vector>

using namespace CommonwealthOnline::Server::RuntimeSupport;

#include "ServerRuntime.inc"
#include "SteamWebApiSetup.h"

#include <cstdio>
#include <iostream>

#ifdef _WIN32
#  define WIN32_LEAN_AND_MEAN
#  include <Windows.h>
#endif

int main(int argc, char** argv)
{
	std::setvbuf(stdout, nullptr, _IONBF, 0);
	std::setvbuf(stderr, nullptr, _IONBF, 0);
	int exitCode = 0;
	if (const auto setupExit = CommonwealthOnline::Server::RunSteamKeySetupIfRequested(argc, argv)) {
		exitCode = *setupExit;
	} else {
		exitCode = CommonwealthOnlineServerMain(argc, argv);
	}
#ifdef _WIN32
	DWORD attachedProcesses[2]{};
	DWORD consoleMode = 0;
	if (exitCode != 0 && exitCode != CommonwealthOnline::Server::kGnsPollRestartExitCode &&
		GetConsoleProcessList(attachedProcesses, 2) == 1 &&
		GetConsoleMode(GetStdHandle(STD_OUTPUT_HANDLE), &consoleMode) &&
		GetFileType(GetStdHandle(STD_INPUT_HANDLE)) == FILE_TYPE_CHAR) {
		std::cout << "Press Enter to close..." << std::endl;
		std::cin.get();
	}
#endif
	return exitCode;
}
