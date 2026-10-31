#include "F4TCharacterCreatorCommands.h"

#include <cassert>
#include <string>
#include <string_view>

namespace
{
	using namespace F4T::CharacterCreator::Commands;

	const CodeObjectCommand& Require(std::string_view a_action)
	{
		const auto* command = FindCommand(a_action);
		assert(command != nullptr);
		return *command;
	}
}

int main()
{
	assert(ValidateCommandTable().empty());

	assert(kCodeObjectCommands[0].action != kCodeObjectCommands[1].action);
	assert(FindCommand("setHairStyle") != nullptr);
	assert(FindCommand("notAnAction") == nullptr);
	assert(FindCommand("") == nullptr);

	assert(std::string_view{ Require("setHairStyle").member } == "ChangeHairStyle");
	assert(std::string_view{ Require("setHairColor").member } == "ChangeHairColor");
	assert(std::string_view{ Require("setBeard").member } == "ChangeBeard");
	assert(std::string_view{ Require("setDetailColor").member } == "ChangeColor");
	assert(std::string_view{ Require("setMorphPreset").member } == "ChangePreset");
	assert(std::string_view{ Require("setMorphIntensity").member } == "ChangePresetIntensity");
	assert(std::string_view{ Require("setAxisMorph").member } == "ChangeBoneRegionMorphSlider");
	assert(std::string_view{ Require("setAxisScale").member } == "ChangeBoneRegionAxis");
	assert(std::string_view{ Require("applyTint").member } == "SetDetailIntensity");
	assert(std::string_view{ Require("removeTint").member } == "ClearTemporaryDetail");
	assert(std::string_view{ Require("confirm").member } == "ConfirmAndCloseMenu");
	assert(std::string_view{ Require("undo").member } == "UndoLastAction");

	assert(ExpectedArity(ArgKind::None) == 0);
	assert(ExpectedArity(ArgKind::Bool) == 1);
	assert(ExpectedArity(ArgKind::Index) == 1);
	assert(ExpectedArity(ArgKind::SignedUnit) == 1);
	assert(ExpectedArity(ArgKind::TintRemove) == 1);
	assert(ExpectedArity(ArgKind::UnitPair) == 2);
	assert(ExpectedArity(ArgKind::MorphPreset) == 2);
	assert(ExpectedArity(ArgKind::AxisValue) == 3);
	assert(ExpectedArity(ArgKind::TintApply) == 4);
	assert(Require("setAxisMorph").argKind == ArgKind::AxisValue);
	assert(Require("applyTint").argKind == ArgKind::TintApply);
	assert(Require("setWeightPoint").argKind == ArgKind::UnitPair);
	assert(Require("setMorphIntensity").argKind == ArgKind::SignedUnit);

	assert(Require("setHairStyle").undoType == 3);
	assert(Require("setHairColor").undoType == 2);
	assert(Require("setBeard").undoType == 4);
	assert(Require("setMorphPreset").undoType == 7);
	assert(Require("setMorphIntensity").undoType == 7);
	assert(Require("applyTint").undoType == 5);
	assert(Require("removeTint").undoType == 5);
	assert(Require("setWeightPoint").undoType == 0);
	assert(Require("setDetailColor").undoType == 6 && Require("setDetailColor").undoWithBone);
	assert(Require("setAxisMorph").undoType == 1 && Require("setAxisMorph").undoWithBone);
	assert(Require("setAxisScale").undoType == 1 && Require("setAxisScale").undoWithBone);
	assert(!Require("setHairStyle").undoWithBone);
	assert(!Require("applyTint").undoWithBone);

	assert(Require("confirm").undoType == kNoUndoPoint);
	assert(Require("undo").undoType == kNoUndoPoint);
	assert(Require("beginBodyEdit").undoType == kNoUndoPoint);
	assert(Require("setSculptMode").undoType == kNoUndoPoint);

	assert(IsKnownControl("Accept"));
	assert(IsKnownControl("Cancel"));
	assert(IsKnownControl("KeyLeft"));
	assert(IsKnownControl("37"));
	assert(!IsKnownControl(""));
	assert(!IsKnownControl("F1"));
	assert(!IsKnownControl("accept"));

	assert(kMaxFeatureId == 8);
	assert(kMaxIndexArg == 4095);

	assert(IsPresetPathAllowed("Data/F4SE/Plugins/F4EE/Presets/Vault Dweller.json"));
	assert(IsPresetPathAllowed(std::string{ kPresetDirectory } + "a.json"));
	assert(!IsPresetPathAllowed(""));
	assert(!IsPresetPathAllowed("Data/F4SE/Plugins/F4EE/Presets/../../../secrets.json"));
	assert(!IsPresetPathAllowed("Data/F4SE/Plugins/F4EE/Presets/..\\escaped.json"));
	assert(!IsPresetPathAllowed("C:/Windows/System32/config/SAM"));
	assert(!IsPresetPathAllowed("Data\\F4SE\\Plugins\\F4EE\\Presets\\a.json"));
	assert(!IsPresetPathAllowed("/Data/F4SE/Plugins/F4EE/Presets/a.json"));

	return 0;
}
