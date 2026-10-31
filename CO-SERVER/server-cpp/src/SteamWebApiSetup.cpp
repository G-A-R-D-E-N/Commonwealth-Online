#include "SteamWebApiSetup.h"

#include "AdminTokenStore.h"
#include "DirectoryRegistrar.h"
#include "SteamWebApiVerifier.h"

#include <cstdlib>
#include <exception>
#include <filesystem>
#include <iostream>
#include <string>
#include <string_view>
#include <system_error>

#if defined(_WIN32)
#	define WIN32_LEAN_AND_MEAN
#	define NOMINMAX
#	include <Windows.h>
#	include <shellapi.h>
#else
#	include <termios.h>
#	include <unistd.h>
#endif

namespace CommonwealthOnline::Server
{
	namespace
	{
		bool StdinIsConsole()
		{
#if defined(_WIN32)
			DWORD mode = 0;
			return GetConsoleMode(GetStdHandle(STD_INPUT_HANDLE), &mode) != 0;
#else
			return isatty(STDIN_FILENO) != 0;
#endif
		}

		void OpenKeyPage()
		{
#if defined(_WIN32)
			const std::wstring page(kSteamWebApiKeyPage.begin(), kSteamWebApiKeyPage.end());
			(void)ShellExecuteW(nullptr, L"open", page.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
#endif
		}

		std::string ReadHiddenLine()
		{
			std::string line;
#if defined(_WIN32)
			const HANDLE input = GetStdHandle(STD_INPUT_HANDLE);
			DWORD mode = 0;
			const bool console = GetConsoleMode(input, &mode) != 0;
			if (console) SetConsoleMode(input, mode & ~static_cast<DWORD>(ENABLE_ECHO_INPUT));
			std::getline(std::cin, line);
			if (console) SetConsoleMode(input, mode);
#else
			termios saved{};
			const bool console = isatty(STDIN_FILENO) != 0 && tcgetattr(STDIN_FILENO, &saved) == 0;
			if (console) {
				termios hidden = saved;
				hidden.c_lflag &= ~static_cast<tcflag_t>(ECHO);
				tcsetattr(STDIN_FILENO, TCSANOW, &hidden);
			}
			std::getline(std::cin, line);
			if (console) tcsetattr(STDIN_FILENO, TCSANOW, &saved);
#endif
			std::cout << '\n';
			const auto first = line.find_first_not_of(" \t\r\n");
			const auto last = line.find_last_not_of(" \t\r\n");
			return first == std::string::npos ? std::string{} : line.substr(first, last - first + 1);
		}

		bool EnvironmentKeySet()
		{
			const std::string name{ kSteamWebApiKeyEnvironment };
#if defined(_WIN32)
			char* value = nullptr;
			std::size_t length = 0;
			const bool set = _dupenv_s(&value, &length, name.c_str()) == 0 && value != nullptr && *value != '\0';
			std::free(value);
			return set;
#else
			const char* value = std::getenv(name.c_str());
			return value != nullptr && *value != '\0';
#endif
		}

		int SetupKey(const std::filesystem::path& a_keyPath)
		{
			std::cout
				<< "Steam ID verification setup\n"
				<< "This lets the server confirm each player's Steam account on direct-IP and dedicated servers.\n\n"
				<< "  1. Open " << kSteamWebApiKeyPage << " and sign in with Steam.\n"
				<< "     The account must not be limited (it has spent at least US$5 on Steam).\n"
				<< "  2. For \"Domain Name\" enter anything, for example localhost, accept the terms and click Register.\n"
				<< "  3. Copy the key and paste it below. It is not shown while you type and is never printed or logged.\n\n";
			if (StdinIsConsole()) {
				OpenKeyPage();
			}
			std::cout << "Steam Web API key: " << std::flush;
			const auto key = ReadHiddenLine();
			if (key.empty()) {
				std::cerr << "No key entered; nothing was saved.\n";
				return 1;
			}
			if (!IsSteamWebApiKey(key)) {
				std::cerr << "That is not a Steam Web API key (expected 32 letters and digits, 0-9 and A-F). Nothing was saved.\n";
				return 1;
			}

			std::cout << "Checking the key with Steam..." << std::endl;
			const auto check = CheckSteamWebApiKey(key, CreatePlatformDirectorySender());
			if (check.outcome == SteamKeyCheck::Rejected) {
				std::cerr << "Steam rejected the key (" << check.detail << "). Nothing was saved.\n"
				          << "Copy the key again from " << kSteamWebApiKeyPage << " and re-run --setup-steam-key.\n";
				return 1;
			}
			try {
				AdminTokenStore::Replace(a_keyPath, key);
			} catch (const std::exception& exception) {
				std::cerr << "Could not save the key: " << exception.what() << '\n';
				return 1;
			}
			if (check.outcome == SteamKeyCheck::Unreachable) {
				std::cout << "Saved, but Steam could not be reached to check the key (" << check.detail << ").\n"
				          << "The server checks it again at startup.\n";
			} else {
				std::cout << "Steam accepted the key.\n";
			}
			std::cout << "Stored in " << a_keyPath.string() << " (readable only by this user).\n"
			          << "Restart the server; its log will show \"Steam ID verification: ON\".\n";
			if (EnvironmentKeySet()) {
				std::cout << "Note: " << kSteamWebApiKeyEnvironment << " is set in this environment and takes priority over the saved file.\n";
			}
			return 0;
		}

		int RemoveKey(const std::filesystem::path& a_keyPath)
		{
			std::error_code error;
			const bool removed = std::filesystem::remove(a_keyPath, error);
			if (error) {
				std::cerr << "Could not remove " << a_keyPath.string() << ": " << error.message() << '\n';
				return 1;
			}
			std::cout << (removed ? "Removed " : "No saved key at ") << a_keyPath.string()
			          << ". Steam ID verification for direct connections is off after the next restart.\n";
			if (EnvironmentKeySet()) {
				std::cout << "Note: " << kSteamWebApiKeyEnvironment << " is still set and keeps verification on.\n";
			}
			return 0;
		}
	}

	std::optional<int> RunSteamKeySetupIfRequested(int argc, char** argv)
	{
		bool setup = false;
		bool remove = false;
		std::optional<std::filesystem::path> configPath;
		for (int index = 1; index < argc; ++index) {
			const std::string_view argument{ argv[index] };
			if (argument == "--setup-steam-key") setup = true;
			else if (argument == "--remove-steam-key") remove = true;
			else if (argument == "--config" && index + 1 < argc) configPath = std::filesystem::absolute(argv[++index]);
		}
		if (!setup && !remove) {
			return std::nullopt;
		}
		if (setup && remove) {
			std::cerr << "Use either --setup-steam-key or --remove-steam-key, not both.\n";
			return 2;
		}
		const auto baseDirectory = configPath ? configPath->parent_path() : std::filesystem::current_path();
		const auto keyPath = baseDirectory / kSteamWebApiKeyFileName;
		return setup ? SetupKey(keyPath) : RemoveKey(keyPath);
	}
}
