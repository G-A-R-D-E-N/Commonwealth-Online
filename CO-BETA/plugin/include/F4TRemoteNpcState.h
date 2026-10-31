#pragma once

#include <chrono>
#include <cstdint>
#include <string>
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

	// source:RE-Work/subsystems/commonwealth-online-re/npc-sync-co250/README.md (W2 review R1, w2r-getnpc-templated-temp-id-og; R5 counter is not comparable across clients)
	constexpr bool IsRuntimeFormId(std::uint32_t a_formId) noexcept
	{
		return a_formId >= 0xFF000000U;
	}

	void ReplaceSnapshot(std::uint32_t a_hostPlayerId, std::vector<NpcState> a_states);
	void Clear();
	std::vector<NpcState> GetSnapshot();
	std::uint32_t GetSnapshotHostPlayerId();
	std::chrono::steady_clock::time_point GetLastReceivedTime();
}
