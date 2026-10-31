#include "pch.h"

#include "F4TRemoteNpcState.h"

#include <mutex>
#include <utility>

namespace
{
	std::mutex g_npcStateMutex;
	std::vector<F4T::RemoteNpcState::NpcState> g_npcStates;
	std::uint32_t g_snapshotHostPlayerId{};
	std::chrono::steady_clock::time_point g_lastReceivedTime{};
}

namespace F4T::RemoteNpcState
{
	void ReplaceSnapshot(std::uint32_t a_hostPlayerId, std::vector<NpcState> a_states)
	{
		const std::scoped_lock lock(g_npcStateMutex);
		g_snapshotHostPlayerId = a_hostPlayerId;
		g_npcStates = std::move(a_states);
		g_lastReceivedTime = std::chrono::steady_clock::now();
	}

	void Clear()
	{
		const std::scoped_lock lock(g_npcStateMutex);
		g_snapshotHostPlayerId = 0;
		g_npcStates.clear();
		g_lastReceivedTime = {};
	}

	std::vector<NpcState> GetSnapshot()
	{
		const std::scoped_lock lock(g_npcStateMutex);
		return g_npcStates;
	}

	std::uint32_t GetSnapshotHostPlayerId()
	{
		const std::scoped_lock lock(g_npcStateMutex);
		return g_snapshotHostPlayerId;
	}

	std::chrono::steady_clock::time_point GetLastReceivedTime()
	{
		const std::scoped_lock lock(g_npcStateMutex);
		return g_lastReceivedTime;
	}
}
