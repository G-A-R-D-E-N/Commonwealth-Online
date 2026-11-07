#pragma once

#include "F4TPowerArmorSync.h"

#include <RE/Fallout.h>

#include <cstdint>
#include <vector>

namespace F4T::PowerArmorSync
{
	enum class MirrorStatus
	{
		kPending,
		kReady,
		kFailed
	};

	struct MirrorFrame
	{
		std::uint32_t baseFormId{};
		std::uint32_t refFormId{};
		std::uint32_t waitTicks{};
		bool piecesApplied{};
		std::uint32_t pieceApplyPasses{};
		std::vector<std::uint32_t> addedPieceFormIds;
		std::uint32_t cellId{};
		std::uint32_t worldspaceId{};
		float x{};
		float y{};
		float z{};
		float angleZ{};
		PowerArmorState appliedPieces;
		RE::NiPointer<RE::TESObjectREFR> reference;
		RE::ObjectRefHandle handle;
	};

	[[nodiscard]] MirrorStatus EnsureMirrorFrame(
		MirrorFrame& a_mirror,
		std::uint32_t a_baseFormId,
		std::uint32_t a_cellId,
		std::uint32_t a_worldspaceId,
		float a_x,
		float a_y,
		float a_z,
		float a_angleZ);
	[[nodiscard]] bool ApplyMirrorPieces(MirrorFrame& a_mirror, const PowerArmorState& a_state);
	void DestroyMirrorFrame(MirrorFrame& a_mirror);
}
