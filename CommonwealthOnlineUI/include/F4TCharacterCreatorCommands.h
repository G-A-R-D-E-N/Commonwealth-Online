#pragma once

#include <cstdint>
#include <memory>
#include <string_view>
#include <vector>

namespace F4T::CharacterCreator::Commands
{
	enum class ArgKind { None, Bool, Index, UnitPair, MorphPreset, TintApply, TintRemove, SignedUnit, AxisValue };

	struct CodeObjectCommand
	{
		const char*   action;
		const char*   member;
		std::uint32_t arity;
		ArgKind       argKind;
		std::int32_t  undoType;
		bool          undoWithBone;
	};

	inline constexpr std::int32_t  kNoUndoPoint = -1;
	inline constexpr std::int64_t  kMaxIndexArg = 4095;
	inline constexpr std::uint32_t kMaxFeatureId = 8;

	inline constexpr CodeObjectCommand kCodeObjectCommands[]{
		{ "confirm", "ConfirmAndCloseMenu", 0, ArgKind::None, kNoUndoPoint, false },
		{ "undo", "UndoLastAction", 0, ArgKind::None, kNoUndoPoint, false },
		{ "changeSex", "ChangeSex", 0, ArgKind::None, kNoUndoPoint, false },
		{ "beginBodyEdit", "StartBodyEdit", 0, ArgKind::None, kNoUndoPoint, false },
		{ "endBodyEdit", "EndBodyEdit", 0, ArgKind::None, kNoUndoPoint, false },
		{ "setSculptMode", "SetSculptMode", 1, ArgKind::Bool, kNoUndoPoint, false },
		{ "setFeatureMode", "SetFeatureMode", 1, ArgKind::Bool, kNoUndoPoint, false },
		{ "setHairStyle", "ChangeHairStyle", 1, ArgKind::Index, 3, false },
		{ "setHairColor", "ChangeHairColor", 1, ArgKind::Index, 2, false },
		{ "setBeard", "ChangeBeard", 1, ArgKind::Index, 4, false },
		{ "setDetailColor", "ChangeColor", 1, ArgKind::Index, 6, true },
		{ "setWeightPoint", "WeightPointChange", 2, ArgKind::UnitPair, 0, false },
		{ "setMorphPreset", "ChangePreset", 2, ArgKind::MorphPreset, 7, false },
		{ "setMorphIntensity", "ChangePresetIntensity", 1, ArgKind::SignedUnit, 7, false },
		{ "setAxisMorph", "ChangeBoneRegionMorphSlider", 3, ArgKind::AxisValue, 1, true },
		{ "setAxisScale", "ChangeBoneRegionAxis", 3, ArgKind::AxisValue, 1, true },
		{ "applyTint", "SetDetailIntensity", 4, ArgKind::TintApply, 5, false },
		{ "removeTint", "ClearTemporaryDetail", 1, ArgKind::TintRemove, 5, false },
	};

	inline constexpr const char* kCreatorControls[]{
		"Done", "Accept", "Cancel", "Left", "Right", "Up", "Down",
		"XButton", "YButton", "LShoulder", "RShoulder", "LTrigger", "RTrigger",
		"F", "B", "X", "R", "T", "C", "Q", "Space",
		"KeyLeft", "KeyRight", "KeyUp", "KeyDown", "37", "39"
	};

	inline constexpr const char* kPresetDirectory = "Data/F4SE/Plugins/F4EE/Presets/";

	[[nodiscard]] inline bool IsPresetPathAllowed(std::string_view a_path) noexcept
	{
		if (a_path.empty() || a_path.size() > 512) {
			return false;
		}
		if (a_path.find("..") != std::string_view::npos) {
			return false;
		}
		if (a_path.find(':') != std::string_view::npos) {
			return false;
		}
		return a_path.starts_with(kPresetDirectory);
	}

	[[nodiscard]] constexpr std::uint32_t ExpectedArity(ArgKind a_kind) noexcept
	{
		switch (a_kind) {
		case ArgKind::None:
			return 0;
		case ArgKind::Bool:
		case ArgKind::Index:
		case ArgKind::TintRemove:
		case ArgKind::SignedUnit:
			return 1;
		case ArgKind::UnitPair:
		case ArgKind::MorphPreset:
			return 2;
		case ArgKind::AxisValue:
			return 3;
		case ArgKind::TintApply:
			return 4;
		default:
			return 0xFFFFFFFFu;
		}
	}

	[[nodiscard]] inline const CodeObjectCommand* FindCommand(std::string_view a_action) noexcept
	{
		for (const auto& command : kCodeObjectCommands) {
			if (a_action == command.action) {
				return std::addressof(command);
			}
		}
		return nullptr;
	}

	[[nodiscard]] inline bool IsKnownControl(std::string_view a_control) noexcept
	{
		for (const auto* control : kCreatorControls) {
			if (a_control == control) {
				return true;
			}
		}
		return false;
	}

	[[nodiscard]] inline std::vector<std::string_view> ValidateCommandTable()
	{
		std::vector<std::string_view> problems;

		for (std::size_t i = 0; i < std::size(kCodeObjectCommands); ++i) {
			const auto& command = kCodeObjectCommands[i];

			if (!command.action || command.action[0] == '\0') {
				problems.push_back("command with an empty action");
				continue;
			}
			if (!command.member || command.member[0] == '\0') {
				problems.push_back("command with an empty code-object member");
			}
			if (command.arity != ExpectedArity(command.argKind)) {
				problems.push_back("command whose declared arity does not match its argument kind");
			}
			if (command.undoType != kNoUndoPoint && (command.undoType < 0 || command.undoType > 7)) {
				problems.push_back("command whose undo type is outside the movie's 0..7 constants");
			}
			if (command.undoType == kNoUndoPoint && command.undoWithBone) {
				problems.push_back("command that passes a bone id to an undo point it never creates");
			}
			for (std::size_t j = i + 1; j < std::size(kCodeObjectCommands); ++j) {
				if (std::string_view{ command.action } == kCodeObjectCommands[j].action) {
					problems.push_back("duplicate action name");
					break;
				}
			}
		}

		for (std::size_t i = 0; i < std::size(kCreatorControls); ++i) {
			for (std::size_t j = i + 1; j < std::size(kCreatorControls); ++j) {
				if (std::string_view{ kCreatorControls[i] } == kCreatorControls[j]) {
					problems.push_back("duplicate control name");
					break;
				}
			}
		}

		return problems;
	}
}
