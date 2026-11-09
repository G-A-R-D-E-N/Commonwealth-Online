#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace VoiceLab
{
	using Handle = void*;
	using CreateFn = int (*)(const char*, std::uint16_t, Handle*, char*, std::size_t);
	using DestroyFn = void (*)(Handle);
	using LocalPortFn = std::uint16_t (*)(Handle);
	using CountFn = std::uint32_t (*)(Handle);

	struct GnsEvent
	{
		std::uint32_t type;
		std::uint32_t connectionId;
		std::int32_t reason;
		std::uint32_t payloadSize;
		char debug[128];
	};

	using PollFn = int (*)(Handle, GnsEvent*, void*, std::uint32_t);
	using SendFn = int (*)(Handle, const void*, std::uint32_t, std::uint32_t);
	using DisconnectFn = int (*)(Handle, std::int32_t, const char*);

	struct Bridge
	{
		CreateFn serverCreate{};
		DestroyFn serverDestroy{};
		LocalPortFn serverLocalPort{};
		CountFn serverConnectionCount{};
		PollFn serverPoll{};
		CreateFn clientCreate{};
		DestroyFn clientDestroy{};
		PollFn clientPoll{};
		SendFn clientSend{};
		DisconnectFn clientDisconnect{};
	};

	bool LoadBridge(const std::string& a_path, Bridge& a_bridge);
}

int RunGns(int a_argc, char** a_argv);
