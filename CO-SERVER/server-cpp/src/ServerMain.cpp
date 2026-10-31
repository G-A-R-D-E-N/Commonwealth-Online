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
	if (exitCode != 0 && GetConsoleProcessList(attachedProcesses, 2) == 1 &&
		GetConsoleMode(GetStdHandle(STD_OUTPUT_HANDLE), &consoleMode) &&
		GetFileType(GetStdHandle(STD_INPUT_HANDLE)) == FILE_TYPE_CHAR) {
		std::cout << "Press Enter to close..." << std::endl;
		std::cin.get();
	}
#endif
	return exitCode;
}
