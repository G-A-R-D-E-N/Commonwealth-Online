#include "F4TSteamInvite.h"

#include "F4TDebugLogging.h"
#include "F4TSteamTunnelService.h"

#include <algorithm>
#include <charconv>
#include <cstring>
#include <map>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

namespace F4T::Steam
{
	namespace
	{
		constexpr std::string_view kSessionLog = "SteamSession";
		constexpr std::size_t kMaximumCommandLine = 32768;
		constexpr std::size_t kMaximumLaunchConnect = kRichPresenceConnectBytes - 1;
		constexpr std::uint64_t kSteamIdUniverseShift = 56;
		constexpr std::uint64_t kSteamIdTypeShift = 52;
		constexpr std::uint64_t kSteamIdInstanceShift = 32;
		constexpr std::uint64_t kSteamIdTypeMask = 0xF;
		constexpr std::uint64_t kSteamIdInstanceMask = 0xFFFFF;
		constexpr std::uint64_t kUniversePublic = 1;
		constexpr std::uint64_t kAccountTypeIndividual = 1;
		constexpr std::uint64_t kAccountTypeChat = 8;
		constexpr std::uint64_t kInstanceDesktop = 1;
		constexpr std::uint64_t kChatInstanceFlagLobby = 0x40000;

		struct InviteCallback
		{
			virtual void Run(void* a_param) = 0;
			virtual void Run(void* a_param, bool a_ioFailure, std::uint64_t a_call) = 0;
			virtual int GetCallbackSizeBytes() = 0;

			std::uint8_t m_nCallbackFlags{};
			int m_iCallback{};
		};

		void RunRichPresenceJoin(const void* a_param);
		void RunLobbyJoinRequested(const void* a_param);
		void RunLobbyCreated(const void* a_param, bool a_ioFailure, std::uint64_t a_call);
		void RunLobbyEntered(const void* a_param, bool a_ioFailure, std::uint64_t a_call);

		struct RichPresenceJoinCallback : InviteCallback
		{
			void Run(void* a_param) override { RunRichPresenceJoin(a_param); }
			void Run(void*, bool, std::uint64_t) override {}
			int GetCallbackSizeBytes() override { return static_cast<int>(sizeof(RichPresenceJoinRequest)); }
		};

		struct LobbyJoinRequestedCallback : InviteCallback
		{
			void Run(void* a_param) override { RunLobbyJoinRequested(a_param); }
			void Run(void*, bool, std::uint64_t) override {}
			int GetCallbackSizeBytes() override { return static_cast<int>(sizeof(GameLobbyJoinRequest)); }
		};

		struct LobbyCreatedCallResult : InviteCallback
		{
			LobbyCreatedCallResult() { m_iCallback = kLobbyCreatedCallback; }
			void Run(void*) override {}
			void Run(void* a_param, bool a_ioFailure, std::uint64_t a_call) override { RunLobbyCreated(a_param, a_ioFailure, a_call); }
			int GetCallbackSizeBytes() override { return static_cast<int>(sizeof(LobbyCreatedResult)); }
		};

		struct LobbyEnterCallResult : InviteCallback
		{
			LobbyEnterCallResult() { m_iCallback = kLobbyEnterCallback; }
			void Run(void*) override {}
			void Run(void* a_param, bool a_ioFailure, std::uint64_t a_call) override { RunLobbyEntered(a_param, a_ioFailure, a_call); }
			int GetCallbackSizeBytes() override { return static_cast<int>(sizeof(LobbyEnterResult)); }
		};

		std::mutex g_bindMutex;
		std::mutex g_mutex;
		InviteBind g_bind{};
		std::string g_code;
		RichPresenceJoinCallback g_callback{};
		LobbyJoinRequestedCallback g_lobbyCallback{};
		LobbyCreatedCallResult g_createdResult{};
		LobbyEnterCallResult g_enteredResult{};
		bool g_registered = false;
		int g_lastCallbackId = 0;
		std::vector<int> g_registeredIds;
		void (*g_connectHandler)(const char*) = nullptr;
		void (*g_lobbyHandler)(std::uint64_t) = &JoinLobby;
		void (*g_failureHandler)(const char*) = nullptr;
		std::map<std::uint64_t, LobbyCreatedFn> g_createdPending;
		std::map<std::uint64_t, LobbyEnteredFn> g_enteredPending;
		LaunchArgs g_launch;

		static_assert(sizeof(InviteCallback) == 16);

		bool AllDigits(std::string_view a_text)
		{
			return !a_text.empty() && std::all_of(a_text.begin(), a_text.end(), [](char a_char) { return a_char >= '0' && a_char <= '9'; });
		}

		std::optional<std::uint64_t> ParseDecimalId(std::string_view a_text)
		{
			if (a_text.empty() || a_text.size() > kMaximumSteamIdDigits || !AllDigits(a_text) || a_text.front() == '0') return std::nullopt;
			std::uint64_t value = 0;
			const auto parsed = std::from_chars(a_text.data(), a_text.data() + a_text.size(), value);
			if (parsed.ec != std::errc{} || parsed.ptr != a_text.data() + a_text.size() || value == 0) return std::nullopt;
			return value;
		}

		std::optional<std::vector<std::string>> Tokenize(std::string_view a_commandLine)
		{
			std::vector<std::string> tokens;
			std::string current;
			bool quoted = false;
			bool pending = false;
			for (const char character : a_commandLine) {
				if (character == '"') {
					quoted = !quoted;
					pending = true;
				} else if (!quoted && (character == ' ' || character == '\t' || character == '\r' || character == '\n')) {
					if (pending) tokens.push_back(std::exchange(current, {}));
					pending = false;
				} else {
					current.push_back(character);
					pending = true;
				}
			}
			if (quoted) return std::nullopt;
			if (pending) tokens.push_back(std::move(current));
			return tokens;
		}

		bool PlainConnectText(std::string_view a_text)
		{
			return !a_text.empty() && a_text.size() <= kMaximumLaunchConnect &&
				std::none_of(a_text.begin(), a_text.end(), [](char a_char) {
					const auto byte = static_cast<unsigned char>(a_char);
					return byte < 0x20 || byte == 0x7F;
				});
		}

		std::string ConnectFieldText(const char* a_field)
		{
			const char* const end = std::find(a_field, a_field + kRichPresenceConnectBytes - 1, '\0');
			return std::string{ a_field, end };
		}

		void RunRichPresenceJoin(const void* a_param)
		{
			if (a_param == nullptr) return;
			RichPresenceJoinRequest request{};
			std::memcpy(&request, a_param, sizeof(request));
			request.connect[kRichPresenceConnectBytes - 1] = '\0';
			const auto text = ConnectFieldText(request.connect);
			F4T::DebugLogging::Log(kSessionLog, "rich presence join callback friend={} connect=\"{}\" length={}",
				request.steamIdFriend, SanitizeForLog(text), text.size());
			if (text.empty()) return;
			(void)HandOffConnect(text, "rich presence");
		}

		void RunLobbyJoinRequested(const void* a_param)
		{
			if (a_param == nullptr) return;
			GameLobbyJoinRequest request{};
			std::memcpy(&request, a_param, sizeof(request));
			F4T::DebugLogging::Log(kSessionLog, "lobby join requested lobby={} friend={}", request.steamIdLobby, request.steamIdFriend);
			if (!IsLobbySteamId(request.steamIdLobby)) {
				F4T::DebugLogging::Log(kSessionLog, "lobby join request rejected: {} is not a lobby id", request.steamIdLobby);
				return;
			}
			void (*handler)(std::uint64_t) = nullptr;
			{
				std::lock_guard lock{ g_mutex };
				handler = g_lobbyHandler;
			}
			if (handler != nullptr) handler(request.steamIdLobby);
		}

		void RunLobbyCreated(const void* a_param, bool a_ioFailure, std::uint64_t a_call)
		{
			LobbyCreatedFn done;
			{
				std::lock_guard lock{ g_mutex };
				const auto pending = g_createdPending.find(a_call);
				if (pending == g_createdPending.end()) return;
				done = std::move(pending->second);
				g_createdPending.erase(pending);
			}
			LobbyCreatedResult result{};
			if (!a_ioFailure && a_param != nullptr) std::memcpy(&result, a_param, sizeof(result));
			const bool ok = !a_ioFailure && a_param != nullptr && result.result == 1 && result.steamIdLobby != 0;
			if (done) done(ok, result.result, result.steamIdLobby);
		}

		void RunLobbyEntered(const void* a_param, bool a_ioFailure, std::uint64_t a_call)
		{
			LobbyEnteredFn done;
			{
				std::lock_guard lock{ g_mutex };
				const auto pending = g_enteredPending.find(a_call);
				if (pending == g_enteredPending.end()) return;
				done = std::move(pending->second);
				g_enteredPending.erase(pending);
			}
			LobbyEnterResult result{};
			if (!a_ioFailure && a_param != nullptr) std::memcpy(&result, a_param, sizeof(result));
			const bool ok = !a_ioFailure && a_param != nullptr && result.response == 1 && result.steamIdLobby != 0;
			if (done) done(ok, result.response, result.steamIdLobby);
		}

		void UnregisterLocked(InviteBind& a_bind, bool& a_registered, std::vector<std::uint64_t>& a_created, std::vector<std::uint64_t>& a_entered)
		{
			a_bind = g_bind;
			a_registered = g_registered;
			for (const auto& pending : g_createdPending) a_created.push_back(pending.first);
			for (const auto& pending : g_enteredPending) a_entered.push_back(pending.first);
			g_createdPending.clear();
			g_enteredPending.clear();
			g_registered = false;
			g_registeredIds.clear();
			g_bind = {};
		}

		void UnbindUnlocked()
		{
			InviteBind previous;
			bool wasRegistered = false;
			std::vector<std::uint64_t> created;
			std::vector<std::uint64_t> entered;
			{
				std::lock_guard lock{ g_mutex };
				UnregisterLocked(previous, wasRegistered, created, entered);
			}
			if (wasRegistered && previous.unregisterCallback) {
				previous.unregisterCallback(&g_lobbyCallback);
				previous.unregisterCallback(&g_callback);
				F4T::DebugLogging::Log(kSessionLog, "steam invite callbacks unregistered");
			}
			if (previous.unregisterCallResult) {
				for (const auto call : created) previous.unregisterCallResult(&g_createdResult, call);
				for (const auto call : entered) previous.unregisterCallResult(&g_enteredResult, call);
			}
		}
	}

	bool IsIndividualSteamId(std::uint64_t a_steamId)
	{
		return ((a_steamId >> kSteamIdUniverseShift) & 0xFF) == kUniversePublic &&
			((a_steamId >> kSteamIdTypeShift) & kSteamIdTypeMask) == kAccountTypeIndividual &&
			((a_steamId >> kSteamIdInstanceShift) & kSteamIdInstanceMask) == kInstanceDesktop &&
			(a_steamId & 0xFFFFFFFFull) != 0;
	}

	bool IsLobbySteamId(std::uint64_t a_steamId)
	{
		return ((a_steamId >> kSteamIdUniverseShift) & 0xFF) == kUniversePublic &&
			((a_steamId >> kSteamIdTypeShift) & kSteamIdTypeMask) == kAccountTypeChat &&
			((a_steamId >> kSteamIdInstanceShift) & kChatInstanceFlagLobby) != 0 &&
			(a_steamId & 0xFFFFFFFFull) != 0;
	}

	std::optional<ConnectTarget> ValidateConnectString(std::string_view a_text)
	{
		const auto value = ParseDecimalId(a_text);
		if (!value) return std::nullopt;
		if (*value < 0x100000000ull) return ConnectTarget{ *value, kSteamId64Base + *value, true };
		if (!IsIndividualSteamId(*value)) return std::nullopt;
		return ConnectTarget{ *value, *value, false };
	}

	LaunchArgs ParseLaunchArgs(std::string_view a_commandLine)
	{
		LaunchArgs args;
		if (a_commandLine.size() > kMaximumCommandLine) return args;
		const auto tokens = Tokenize(a_commandLine);
		if (!tokens) return args;
		bool lobbySeen = false;
		bool connectSeen = false;
		for (std::size_t index = 0; index < tokens->size(); ++index) {
			const auto& token = (*tokens)[index];
			const bool isLobby = token == "+connect_lobby";
			const bool isConnect = token == "+connect";
			if (!isLobby && !isConnect) continue;
			const bool hasValue = index + 1 < tokens->size() && !(*tokens)[index + 1].starts_with('+');
			const std::string_view value = hasValue ? std::string_view{ (*tokens)[index + 1] } : std::string_view{};
			if (hasValue) ++index;
			if (isLobby && !std::exchange(lobbySeen, true)) args.lobbyId = ParseDecimalId(value);
			if (isConnect && !std::exchange(connectSeen, true) && PlainConnectText(value)) args.connect = std::string{ value };
		}
		return args;
	}

	std::string SanitizeForLog(std::string_view a_text, std::size_t a_maximum)
	{
		std::string clean;
		for (const char character : a_text) {
			if (clean.size() >= a_maximum) {
				clean += "...";
				break;
			}
			const auto byte = static_cast<unsigned char>(character);
			clean.push_back(byte < 0x20 || byte >= 0x7F || character == '"' || character == '\\' ? '?' : character);
		}
		return clean;
	}

	void BindInvite(const InviteBind& a_bind)
	{
		std::lock_guard bindLock{ g_bindMutex };
		UnbindUnlocked();
		{
			std::lock_guard lock{ g_mutex };
			g_bind = a_bind;
		}
		if (!a_bind.registerCallback) {
			F4T::DebugLogging::Log(kSessionLog, "steam invite callbacks not registered: SteamAPI_RegisterCallback is not exported");
			return;
		}
		a_bind.registerCallback(&g_lobbyCallback, kGameLobbyJoinRequestedCallback);
		a_bind.registerCallback(&g_callback, kRichPresenceJoinCallback);
		{
			std::lock_guard lock{ g_mutex };
			g_lastCallbackId = kRichPresenceJoinCallback;
			g_registeredIds = { kGameLobbyJoinRequestedCallback, kRichPresenceJoinCallback };
			g_registered = true;
		}
		F4T::DebugLogging::Log(kSessionLog, "steam invite callbacks registered ids={},{}", kGameLobbyJoinRequestedCallback, kRichPresenceJoinCallback);
	}

	void UnbindInvite()
	{
		std::lock_guard bindLock{ g_bindMutex };
		UnbindUnlocked();
	}

	void SetJoinConnectHandler(void (*a_handler)(const char*))
	{
		std::lock_guard lock{ g_mutex };
		g_connectHandler = a_handler;
	}

	void SetLobbyJoinHandler(void (*a_handler)(std::uint64_t))
	{
		std::lock_guard lock{ g_mutex };
		g_lobbyHandler = a_handler;
	}

	void SetJoinFailureHandler(void (*a_handler)(const char*))
	{
		std::lock_guard lock{ g_mutex };
		g_failureHandler = a_handler;
	}

	void ReportJoinFailure(std::string_view a_message)
	{
		void (*handler)(const char*) = nullptr;
		{
			std::lock_guard lock{ g_mutex };
			handler = g_failureHandler;
		}
		F4T::DebugLogging::Log(kSessionLog, "join refused: {}", SanitizeForLog(a_message, 160));
		if (handler != nullptr) handler(std::string{ a_message }.c_str());
	}

	bool HandOffConnect(std::string_view a_connect, std::string_view a_source)
	{
		const auto target = ValidateConnectString(a_connect);
		if (!target) {
			F4T::DebugLogging::Log(kSessionLog, "{} connect rejected: \"{}\" is not a friend code or an individual SteamID64", a_source, SanitizeForLog(a_connect));
			ReportJoinFailure("That Steam invite is not valid.");
			return false;
		}
		void (*handler)(const char*) = nullptr;
		{
			std::lock_guard lock{ g_mutex };
			handler = g_connectHandler;
		}
		if (handler == nullptr) {
			F4T::DebugLogging::Log(kSessionLog, "{} connect dropped: no join handler is set", a_source);
			ReportJoinFailure("Joining is not ready yet. Try the invite again in a moment.");
			return false;
		}
		const std::string text{ a_connect };
		F4T::DebugLogging::Log(kSessionLog, "{} connect accepted target={} friendCode={}", a_source, target->value, target->friendCode);
		handler(text.c_str());
		return true;
	}

	void QueueLaunchJoin(const LaunchArgs& a_args)
	{
		std::lock_guard lock{ g_mutex };
		g_launch = a_args;
	}

	bool FireLaunchJoin()
	{
		LaunchArgs args;
		{
			std::lock_guard lock{ g_mutex };
			args = std::exchange(g_launch, LaunchArgs{});
		}
		if (args.lobbyId) {
			F4T::DebugLogging::Log(kSessionLog, "launch join lobby={}", *args.lobbyId);
			void (*handler)(std::uint64_t) = nullptr;
			{
				std::lock_guard lock{ g_mutex };
				handler = g_lobbyHandler;
			}
			if (!IsLobbySteamId(*args.lobbyId)) {
				F4T::DebugLogging::Log(kSessionLog, "launch join lobby rejected: {} is not a lobby id", *args.lobbyId);
				ReportJoinFailure("That Steam lobby invite is not valid.");
				return false;
			}
			if (handler == nullptr) {
				F4T::DebugLogging::Log(kSessionLog, "launch join lobby dropped: no lobby join handler is set");
				ReportJoinFailure("Joining is not ready yet. Try the invite again in a moment.");
				return false;
			}
			handler(*args.lobbyId);
			return true;
		}
		if (args.connect) return HandOffConnect(*args.connect, "launch");
		return false;
	}

	bool AwaitLobbyCreated(std::uint64_t a_call, LobbyCreatedFn a_done)
	{
		InviteBind bind;
		{
			std::lock_guard lock{ g_mutex };
			bind = g_bind;
			if (!bind.registerCallResult || a_call == 0 || g_createdPending.contains(a_call)) return false;
			g_createdPending.emplace(a_call, std::move(a_done));
		}
		bind.registerCallResult(&g_createdResult, a_call);
		return true;
	}

	bool AwaitLobbyEntered(std::uint64_t a_call, LobbyEnteredFn a_done)
	{
		InviteBind bind;
		{
			std::lock_guard lock{ g_mutex };
			bind = g_bind;
			if (!bind.registerCallResult || a_call == 0 || g_enteredPending.contains(a_call)) return false;
			g_enteredPending.emplace(a_call, std::move(a_done));
		}
		bind.registerCallResult(&g_enteredResult, a_call);
		return true;
	}

	void RememberInviteCode(std::uint64_t a_friendCode)
	{
		if (a_friendCode == 0) return;
		const auto text = std::to_string(a_friendCode);
		{
			std::lock_guard lock{ g_mutex };
			g_code = text;
		}
		SetJoinConnectString(text);
	}

	void ClearInviteCode()
	{
		std::lock_guard lock{ g_mutex };
		g_code.clear();
	}

	std::string OpenFriendInvite()
	{
		const auto fail = [](std::string a_reason) {
			F4T::DebugLogging::Log(kSessionLog, "invite not opened: {}", a_reason);
			return a_reason;
		};
		if (!HostRunning()) return fail("Start hosting before inviting friends.");
		std::string code;
		InviteBind bind;
		{
			std::lock_guard lock{ g_mutex };
			code = g_code;
			bind = g_bind;
		}
		if (code.empty()) return fail("Start hosting before inviting friends.");
		const auto lobby = HostLobby();
		const bool byLobby = lobby != 0 && static_cast<bool>(bind.activateLobby);
		if ((!byLobby && !bind.activate) || bind.friends == nullptr) return fail("Steam invite overlay is not available.");
		if (bind.overlayEnabled && !bind.overlayEnabled()) {
			return fail("The Steam overlay is turned off, so the invite window can't open. Turn on the Steam overlay in Steam settings, or share your Friend Code.");
		}
		if (byLobby) {
			bind.activateLobby(bind.friends, lobby);
			F4T::DebugLogging::Log(kSessionLog, "invite dialog opened for lobby {}", lobby);
			return {};
		}
		bind.activate(bind.friends, code.c_str());
		F4T::DebugLogging::Log(kSessionLog, "invite dialog opened with connect string {}", code);
		return {};
	}

	int InviteCallbackBytes()
	{
		return g_callback.GetCallbackSizeBytes();
	}

	int LastRegisteredCallbackId()
	{
		std::lock_guard lock{ g_mutex };
		return g_lastCallbackId;
	}

	std::vector<int> RegisteredCallbackIds()
	{
		std::lock_guard lock{ g_mutex };
		return g_registeredIds;
	}

	void RunInviteCallbackForTest(const void* a_param)
	{
		g_callback.Run(const_cast<void*>(a_param));
	}

	void RunLobbyJoinCallbackForTest(const void* a_param)
	{
		g_lobbyCallback.Run(const_cast<void*>(a_param));
	}

	void RunLobbyCreatedForTest(const void* a_param, bool a_ioFailure, std::uint64_t a_call)
	{
		g_createdResult.Run(const_cast<void*>(a_param), a_ioFailure, a_call);
	}

	void RunLobbyEnteredForTest(const void* a_param, bool a_ioFailure, std::uint64_t a_call)
	{
		g_enteredResult.Run(const_cast<void*>(a_param), a_ioFailure, a_call);
	}
}
