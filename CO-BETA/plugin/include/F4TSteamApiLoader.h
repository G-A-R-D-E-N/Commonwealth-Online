// Copyright (C) 2026 FatherLamb. Portions derived from FathersFallout, GPL-3.0 with additional terms. Used with the author's permission.

#pragma once

#include "F4TSteamInvite.h"
#include "F4TSteamTunnel.h"

#include <cstdint>
#include <functional>
#include <span>
#include <string>
#include <vector>

namespace F4T::Steam
{
	inline constexpr const char* kProbeVariable = "COMMONWEALTH_ONLINE_STEAM_PROBE";

	struct SteamFriendApi
	{
		std::function<std::string()> myName;
		std::function<int()> friendCount;
		std::function<std::uint64_t(int)> friendByIndex;
		std::function<std::string(std::uint64_t)> friendName;
		std::function<std::string(std::uint64_t, const char*)> richPresence;
		std::function<void(std::uint64_t)> requestRichPresence;
		std::function<bool(const char*, const char*)> setRichPresence;
		std::function<void()> clearRichPresence;
		std::function<bool(std::uint64_t)> playingFallout;
	};

	struct SteamMatchmakingApi
	{
		std::function<void(int, LobbyCreatedFn)> createLobby;
		std::function<void(std::uint64_t, LobbyEnteredFn)> joinLobby;
		std::function<void(std::uint64_t)> leaveLobby;
		std::function<bool(std::uint64_t, const char*, const char*)> setLobbyData;
		std::function<std::string(std::uint64_t, const char*)> getLobbyData;
		std::function<std::uint64_t(std::uint64_t)> lobbyOwner;
		std::function<int(std::uint64_t)> memberCount;
		std::function<bool(std::uint64_t, int)> setMemberLimit;
		std::function<bool(std::uint64_t, bool)> setJoinable;
	};

	struct SteamApiLoadResult
	{
		bool available{ false };
		std::string reason;
		std::vector<std::string> foundVersions;
		std::vector<std::string> missingOptional;
		bool matchmakingAvailable{ false };
		std::vector<std::string> matchmakingMissing;
		SteamMatchmakingApi matchmaking;
		std::vector<std::string> interfaceProbe;
		std::string moduleVersion;
		std::uint64_t mySteamId{};
		std::function<bool(std::uint64_t)> isFriend;
		SteamFriendApi friends;
		Transport::SteamApi api;
		std::function<std::vector<Transport::SteamConnectionEvent>()> guestConnectionEvents;
		std::function<std::vector<std::uint8_t>()> createAuthTicket;
	};

	struct SteamApiLoadOptions
	{
		bool probe{};
	};

	struct InterfaceProbeInputs
	{
		std::function<void*()> steamClient;
		std::function<std::int32_t()> steamUser;
		std::function<std::int32_t()> steamPipe;
		std::function<void*(void*, std::int32_t, std::int32_t, const char*)> genericInterface;
		void* socketsFromAccessor{};
		void* utilsFromAccessor{};
	};

	[[nodiscard]] std::vector<std::string> ProbeGenericInterfaces(const InterfaceProbeInputs& a_inputs);

	[[nodiscard]] std::span<const char* const> FriendsAccessorNames();
	[[nodiscard]] std::span<const char* const> UserAccessorNames();

	[[nodiscard]] const char* ChooseFirstPresentName(
		std::span<const char* const> a_names,
		const std::function<void*(const char*)>& a_lookup);

	[[nodiscard]] SteamApiLoadOptions ReadLoadOptions(const char* a_probeValue);
	[[nodiscard]] SteamApiLoadResult LoadSteamApi();
	[[nodiscard]] SteamApiLoadResult LoadSteamApiFrom(const std::function<void*(const char*)>& a_findExport, const SteamApiLoadOptions& a_options);
	[[nodiscard]] std::string DescribeVersionResource(std::span<const std::uint8_t> a_resource);
}
