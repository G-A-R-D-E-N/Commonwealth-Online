#include "pch.h"

#include "F4TRemoteNpcState.h"

#include <mutex>
#include <utility>

namespace
{
	std::mutex g_npcStateMutex;
	F4T::RemoteNpcState::SnapshotCache g_npcStates;
}

namespace F4T::RemoteNpcState
{
	void ReplaceSnapshot(Scope a_scope, std::uint32_t a_hostPlayerId, std::uint32_t a_epoch, std::vector<NpcState> a_states)
	{
		const std::scoped_lock lock(g_npcStateMutex);
		g_npcStates.Replace(std::move(a_scope), a_hostPlayerId, a_epoch, std::move(a_states), std::chrono::steady_clock::now());
	}

	void Clear()
	{
		const std::scoped_lock lock(g_npcStateMutex);
		g_npcStates.Clear();
	}

	std::vector<ScopedState> GetSnapshot()
	{
		const std::scoped_lock lock(g_npcStateMutex);
		return g_npcStates.Snapshot(std::chrono::steady_clock::now(), std::chrono::seconds{2});
	}
}
