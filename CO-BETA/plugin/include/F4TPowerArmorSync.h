#pragma once

#include "F4TEquipmentState.h"
#include "F4TRemotePlayerState.h"

#include <algorithm>
#include <charconv>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

namespace RE
{
	class Actor;
	class PlayerCharacter;
}

namespace F4T::PowerArmorSync
{
	using Piece = EquipmentState::Item;

	inline constexpr std::size_t kMaximumPieces = 8;
	inline constexpr std::size_t kMaximumPieceMods = EquipmentState::kMaximumItemMods;
	inline constexpr std::size_t kMaximumPieceSlotChars = 64;

	struct PowerArmorState
	{
		bool inFrame{ false };
		std::uint32_t frameBaseFormId{ 0 };
		std::vector<Piece> pieces;

		bool operator==(const PowerArmorState&) const = default;
	};

	enum class PowerArmorOpKind
	{
		kUnequip,
		kEquip
	};

	struct PowerArmorOp
	{
		PowerArmorOpKind kind{ PowerArmorOpKind::kUnequip };
		Piece piece;

		bool operator==(const PowerArmorOp&) const = default;
	};

	[[nodiscard]] inline std::optional<std::uint32_t> ReadFormId(std::string_view a_text)
	{
		if (a_text.empty() || a_text.size() > 8) {
			return std::nullopt;
		}

		std::uint32_t value{};
		const auto result = std::from_chars(a_text.data(), a_text.data() + a_text.size(), value, 16);
		if (result.ec != std::errc{} || result.ptr != a_text.data() + a_text.size()) {
			return std::nullopt;
		}

		return value;
	}

	[[nodiscard]] inline bool IsValid(const PowerArmorState& a_state)
	{
		if (a_state.pieces.size() > kMaximumPieces) {
			return false;
		}

		std::vector<std::string_view> seenSlots;
		seenSlots.reserve(a_state.pieces.size());
		for (const auto& piece : a_state.pieces) {
			if (piece.formId == 0 || piece.slot.empty() || piece.slot.size() > kMaximumPieceSlotChars) {
				return false;
			}
			if (std::ranges::find(seenSlots, std::string_view{ piece.slot }) != seenSlots.end()) {
				return false;
			}
			seenSlots.push_back(piece.slot);
			if (piece.mods && piece.mods->size() > kMaximumPieceMods) {
				return false;
			}
		}

		return true;
	}

	[[nodiscard]] inline std::optional<PowerArmorState> PublishableCapture(PowerArmorState a_state)
	{
		if (!IsValid(a_state)) {
			return std::nullopt;
		}
		return a_state;
	}

	[[nodiscard]] inline std::optional<nlohmann::json> Encode(const PowerArmorState& a_state)
	{
		if (!IsValid(a_state)) {
			return std::nullopt;
		}

		auto pieces = EquipmentState::Encode(a_state.pieces);
		if (!pieces) {
			return std::nullopt;
		}

		return nlohmann::json{
			{ "inFrame", a_state.inFrame },
			{ "frameBaseFormId", a_state.frameBaseFormId == 0 ? std::string{} : std::format("{:08X}", a_state.frameBaseFormId) },
			{ "pieces", std::move(*pieces) }
		};
	}

	[[nodiscard]] inline std::optional<PowerArmorState> Decode(const nlohmann::json& a_powerArmor)
	{
		if (!a_powerArmor.is_object()) {
			return std::nullopt;
		}

		const auto inFrame = a_powerArmor.find("inFrame");
		const auto frameFormId = a_powerArmor.find("frameBaseFormId");
		const auto pieces = a_powerArmor.find("pieces");
		if (inFrame == a_powerArmor.end() || !inFrame->is_boolean() ||
			frameFormId == a_powerArmor.end() || !frameFormId->is_string() ||
			pieces == a_powerArmor.end()) {
			return std::nullopt;
		}

		PowerArmorState state;
		state.inFrame = inFrame->get<bool>();

		const auto frameText = frameFormId->get<std::string>();
		if (!frameText.empty()) {
			const auto parsed = ReadFormId(frameText);
			if (!parsed) {
				return std::nullopt;
			}
			state.frameBaseFormId = *parsed;
		}

		auto decodedPieces = EquipmentState::Decode(*pieces);
		if (!decodedPieces) {
			return std::nullopt;
		}
		state.pieces = std::move(*decodedPieces);

		if (!IsValid(state)) {
			return std::nullopt;
		}

		return state;
	}

	[[nodiscard]] inline std::vector<PowerArmorOp> PlanPieceOperations(const PowerArmorState& a_previous, const PowerArmorState& a_current)
	{
		const auto findPiece = [](const std::vector<Piece>& a_pieces, std::string_view a_slot) -> const Piece* {
			for (const auto& piece : a_pieces) {
				if (piece.slot == a_slot) {
					return std::addressof(piece);
				}
			}
			return nullptr;
		};
		const auto samePiece = [](const Piece& a_left, const Piece& a_right) {
			if (a_left.formId != a_right.formId) {
				return false;
			}
			return EquipmentState::SameMods(
				a_left.mods.value_or(std::vector<RemotePlayerState::RemoteItemMod>{}),
				a_right.mods.value_or(std::vector<RemotePlayerState::RemoteItemMod>{}));
		};

		const std::vector<Piece> noPieces;
		const auto& currentPieces = a_current.inFrame ? a_current.pieces : noPieces;

		std::vector<PowerArmorOp> ops;
		for (const auto& piece : a_previous.pieces) {
			const auto* current = findPiece(currentPieces, piece.slot);
			if (!current || current->formId != piece.formId) {
				ops.push_back({ PowerArmorOpKind::kUnequip, piece });
			}
		}
		for (const auto& piece : currentPieces) {
			const auto* previous = findPiece(a_previous.pieces, piece.slot);
			if (!previous || !samePiece(*previous, piece)) {
				ops.push_back({ PowerArmorOpKind::kEquip, piece });
			}
		}

		return ops;
	}

	[[nodiscard]] inline PowerArmorState NormalizeForApply(PowerArmorState a_state)
	{
		if (!a_state.inFrame) {
			a_state.pieces.clear();
		}
		return a_state;
	}

	struct PowerArmorApplyResult
	{
		bool changed{ false };
		PowerArmorState applied;
	};

	struct PlacedPowerArmorFrame
	{
		std::uint32_t baseFormId{ 0 };
		std::uint32_t refFormId{ 0 };
		std::uint32_t waitTicks{ 0 };
		std::uint32_t enterPollTicks{ 0 };
		std::uint32_t placements{ 0 };
		bool entered{ false };
		std::vector<std::uint32_t> preexistingRefFormIds;
	};

	[[nodiscard]] inline PowerArmorState AppliedStateAfterOperations(
		const PowerArmorState& a_previous,
		const PowerArmorState& a_current,
		std::span<const PowerArmorOp> a_ops,
		std::span<const std::uint8_t> a_succeeded)
	{
		PowerArmorState applied = a_previous;
		for (std::size_t i = 0; i < a_ops.size() && i < a_succeeded.size(); ++i) {
			if (!a_succeeded[i]) {
				continue;
			}
			const auto& slot = a_ops[i].piece.slot;
			const auto existing = std::ranges::find_if(applied.pieces, [&](const Piece& a_piece) { return a_piece.slot == slot; });
			if (a_ops[i].kind == PowerArmorOpKind::kUnequip) {
				if (existing != applied.pieces.end()) {
					applied.pieces.erase(existing);
				}
				continue;
			}
			if (existing != applied.pieces.end()) {
				*existing = a_ops[i].piece;
			} else {
				applied.pieces.push_back(a_ops[i].piece);
			}
		}
		applied.inFrame = a_current.inFrame;
		applied.frameBaseFormId = a_current.frameBaseFormId;
		return applied;
	}

	// TODO-verify: engine calls unverified on all runtimes.
	[[nodiscard]] std::optional<PowerArmorState> CaptureLocalPowerArmor(
		RE::PlayerCharacter& a_player,
		const std::vector<Piece>& a_equippedItems,
		const PowerArmorState* a_previous = nullptr);
	[[nodiscard]] bool ProxyReportsInPowerArmor(const RE::Actor& a_proxy);
	[[nodiscard]] bool IsPowerArmorPieceForm(std::uint32_t a_formId);
	[[nodiscard]] PowerArmorApplyResult ApplyProxyPowerArmorPlan(
		RE::Actor& a_proxy,
		const PowerArmorState& a_previous,
		const PowerArmorState& a_current,
		std::span<const PowerArmorOp> a_ops,
		std::string_view a_logKey);
	void DestroyPlacedFrame(PlacedPowerArmorFrame& a_frame, RE::Actor* a_proxy);
	bool ApplyProxyPowerArmorFrame(RE::Actor& a_proxy, std::uint32_t a_frameBaseFormId, PlacedPowerArmorFrame& a_frame, std::string_view a_logKey);
	bool UnequipProxyPowerArmorFrame(RE::Actor& a_proxy, PlacedPowerArmorFrame& a_frame, std::string_view a_logKey);
}
