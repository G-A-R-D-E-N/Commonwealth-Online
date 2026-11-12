#pragma once

#include <chrono>
#include <compare>
#include <cstdint>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace F4T::RemoteNpcState
{
	struct NpcState
	{
		std::uint32_t npcId{};
		std::uint32_t baseFormId{};
		std::uint32_t sourceFormId{};
		float x{};
		float y{};
		float z{};
		float angleZ{};
		std::string cellId;
		std::string worldspaceId;
		bool isDead{ false };
		bool isMoving{ false };
		bool isSprinting{ false };
		bool isSneaking{ false };
		float movementSpeed{};
		bool hasHealth{ false };
		float health{};
		float maxHealth{};
	};

	struct Scope
	{
		std::string cellId;
		std::string worldspaceId;
		auto operator<=>(const Scope&) const = default;
	};

	struct ScopedState
	{
		Scope scope;
		std::uint32_t hostPlayerId{};
		std::uint32_t epoch{};
		NpcState state;
	};

	struct NpcKey
	{
		Scope scope;
		std::uint32_t hostPlayerId{};
		std::uint32_t epoch{};
		std::uint32_t npcId{};
		auto operator<=>(const NpcKey&) const = default;
	};

	class SnapshotCache
	{
	public:
		using Clock = std::chrono::steady_clock;
		void Replace(Scope scope, std::uint32_t host, std::uint32_t epoch, std::vector<NpcState> states, Clock::time_point now)
		{
			for (auto it = _snapshots.begin(); it != _snapshots.end();) {
				if (now - it->second.received > std::chrono::seconds{2}) it = _snapshots.erase(it);
				else ++it;
			}
			const auto existing = _snapshots.find(scope);
			if (existing != _snapshots.end() && (epoch < existing->second.epoch ||
				(epoch == existing->second.epoch && host != existing->second.host))) {
				return;
			}
			_snapshots.insert_or_assign(std::move(scope), Entry{host, epoch, std::move(states), now});
		}
		std::vector<ScopedState> Snapshot(Clock::time_point now, Clock::duration timeout)
		{
			std::vector<ScopedState> result;
			for (auto it = _snapshots.begin(); it != _snapshots.end();) {
				if (now - it->second.received > timeout) {
					it = _snapshots.erase(it);
					continue;
				}
				for (const auto& state : it->second.states) {
					result.push_back({it->first, it->second.host, it->second.epoch, state});
				}
				++it;
			}
			return result;
		}
		void Clear() { _snapshots.clear(); }
	private:
		struct Entry { std::uint32_t host; std::uint32_t epoch; std::vector<NpcState> states; Clock::time_point received; };
		std::map<Scope, Entry> _snapshots;
	};

	// source:RE-Work/subsystems/commonwealth-online-re/npc-sync-co250/README.md (W2 review R1, w2r-getnpc-templated-temp-id-og; R5 counter is not comparable across clients)
	constexpr bool IsRuntimeFormId(std::uint32_t a_formId) noexcept
	{
		return a_formId >= 0xFF000000U;
	}

	void ReplaceSnapshot(Scope a_scope, std::uint32_t a_hostPlayerId, std::uint32_t a_epoch, std::vector<NpcState> a_states);
	void Clear();
	std::vector<ScopedState> GetSnapshot();
}
