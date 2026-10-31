#include "F4TPowerArmorSync.h"

#include <cassert>
#include <format>
#include <string>
#include <vector>
// TODO-verify: concept-level wire and plan checks, engine facts unverified; source:docs/code-notes/power-armor-sync.md.

int main()
{
	using namespace F4T;
	using Json = nlohmann::json;
	using Piece = PowerArmorSync::Piece;
	using PowerArmorOpKind = PowerArmorSync::PowerArmorOpKind;
	using PowerArmorState = PowerArmorSync::PowerArmorState;

	static_assert(PowerArmorSync::kMaximumPieces == 8);
	static_assert(PowerArmorSync::kMaximumPieceMods == EquipmentState::kMaximumItemMods);

	const Json recipe = Json::array({ { { "mod", "00054321" }, { "index", 2 }, { "rank", 3 } } });
	const Json wire = Json{
		{ "inFrame", true },
		{ "frameBaseFormId", "0010A0FF" },
		{ "pieces", Json::array({
			Json{ { "slot", "body" }, { "formId", "00011111" } },
			Json{ { "slot", "headband" }, { "formId", "00022222" }, { "mods", recipe } } }) }
	};

	const auto decoded = PowerArmorSync::Decode(wire);
	assert(decoded && decoded->inFrame);
	assert(decoded->frameBaseFormId == 0x0010A0FFU);
	assert(decoded->pieces.size() == 2);
	assert(!decoded->pieces[0].mods);
	assert(decoded->pieces[1].mods && decoded->pieces[1].mods->size() == 1);
	assert(decoded->pieces[1].mods->at(0) == (RemotePlayerState::RemoteItemMod{ 0x00054321, 2, 3 }));
	assert(PowerArmorSync::IsValid(*decoded));
	assert(PowerArmorSync::Encode(*decoded) == wire);

	const PowerArmorState none;
	const auto noneWire = PowerArmorSync::Encode(none);
	assert(noneWire && (*noneWire)["inFrame"] == false);
	assert((*noneWire)["frameBaseFormId"] == "");
	assert((*noneWire)["pieces"] == Json::array());
	assert(PowerArmorSync::Decode(*noneWire) == none);

	auto broken = wire;
	broken["inFrame"] = "yes";
	assert(!PowerArmorSync::Decode(broken));
	broken = wire;
	broken["frameBaseFormId"] = 1;
	assert(!PowerArmorSync::Decode(broken));
	broken = wire;
	broken.erase("pieces");
	assert(!PowerArmorSync::Decode(broken));
	broken = wire;
	broken.erase("frameBaseFormId");
	assert(!PowerArmorSync::Decode(broken));
	broken = wire;
	broken.erase("inFrame");
	assert(!PowerArmorSync::Decode(broken));
	broken = wire;
	broken["pieces"] = Json::object();
	assert(!PowerArmorSync::Decode(broken));
	assert(!PowerArmorSync::Decode(Json::array()));
	assert(!PowerArmorSync::Decode(Json{ { "inFrame", true } }));

	broken = wire;
	broken["frameBaseFormId"] = "ZZZZ";
	assert(!PowerArmorSync::Decode(broken));
	broken = wire;
	broken["frameBaseFormId"] = "123456789";
	assert(!PowerArmorSync::Decode(broken));
	broken = wire;
	broken["frameBaseFormId"] = "1234";
	const auto shortForm = PowerArmorSync::Decode(broken);
	assert(shortForm && shortForm->frameBaseFormId == 0x1234U);
	assert(PowerArmorSync::Encode(*shortForm)->at("frameBaseFormId") == "00001234");

	broken = wire;
	broken["pieces"] = Json::array({ Json{ { "slot", "body" }, { "formId", "" } } });
	assert(!PowerArmorSync::Decode(broken));
	broken["pieces"] = Json::array({ Json{ { "slot", "body" }, { "formId", "00000000" } } });
	assert(!PowerArmorSync::Decode(broken));
	broken["pieces"] = Json::array({ Json{ { "slot", "" }, { "formId", "00011111" } } });
	assert(!PowerArmorSync::Decode(broken));
	broken["pieces"] = Json::array({ Json{ { "slot", "body" }, { "formId", 0x1111 } } });
	assert(!PowerArmorSync::Decode(broken));
	broken["pieces"] = Json::array({
		Json{ { "slot", "body" }, { "formId", "00011111" } },
		Json{ { "slot", "body" }, { "formId", "00022222" } } });
	assert(!PowerArmorSync::Decode(broken));

	Json ninePieces = Json::array();
	for (int i = 0; i < 9; ++i) {
		ninePieces.push_back(Json{ { "slot", std::format("slot{}", i) }, { "formId", std::format("{:08X}", 0x1000 + i) } });
	}
	broken = wire;
	broken["pieces"] = ninePieces;
	assert(!PowerArmorSync::Decode(broken));
	ninePieces.erase(ninePieces.begin() + 8);
	broken["pieces"] = ninePieces;
	const auto eightPieces = PowerArmorSync::Decode(broken);
	assert(eightPieces && eightPieces->pieces.size() == 8);
	assert(PowerArmorSync::Encode(*eightPieces) == broken);

	Json bigRecipe = Json::array();
	for (int i = 0; i < 33; ++i) {
		bigRecipe.push_back(Json{ { "mod", "00054321" }, { "index", 0 }, { "rank", 0 } });
	}
	broken = wire;
	broken["pieces"] = Json::array({ Json{ { "slot", "body" }, { "formId", "00011111" }, { "mods", bigRecipe } } });
	assert(!PowerArmorSync::Decode(broken));
	bigRecipe.erase(bigRecipe.begin() + 32);
	broken["pieces"] = Json::array({ Json{ { "slot", "body" }, { "formId", "00011111" }, { "mods", bigRecipe } } });
	const auto fullRecipe = PowerArmorSync::Decode(broken);
	assert(fullRecipe && fullRecipe->pieces.at(0).mods->size() == 32);
	assert(PowerArmorSync::Encode(*fullRecipe) == broken);

	PowerArmorState tooMany;
	for (int i = 0; i < 9; ++i) {
		tooMany.pieces.push_back(Piece{ std::format("slot{}", i), 0x1000U + i, std::nullopt });
	}
	assert(!PowerArmorSync::Encode(tooMany));
	PowerArmorState zeroForm;
	zeroForm.pieces.push_back(Piece{ "body", 0, std::nullopt });
	assert(!PowerArmorSync::Encode(zeroForm));
	PowerArmorState duplicateSlot;
	duplicateSlot.pieces.push_back(Piece{ "body", 0x1111, std::nullopt });
	duplicateSlot.pieces.push_back(Piece{ "body", 0x2222, std::nullopt });
	assert(!PowerArmorSync::Encode(duplicateSlot));
	assert(!PowerArmorSync::IsValid(tooMany));
	assert(!PowerArmorSync::IsValid(zeroForm));
	assert(!PowerArmorSync::IsValid(duplicateSlot));

	PowerArmorState inFrame;
	inFrame.inFrame = true;
	inFrame.frameBaseFormId = 0x0010A0FF;
	inFrame.pieces.push_back(Piece{ "body", 0x1111, std::nullopt });
	inFrame.pieces.push_back(Piece{ "headband", 0x2222, std::nullopt });

	assert(PowerArmorSync::PlanPieceOperations(none, none).empty());
	assert(PowerArmorSync::PlanPieceOperations(inFrame, inFrame).empty());

	assert(!PowerArmorSync::PublishableCapture(tooMany));
	assert(!PowerArmorSync::PublishableCapture(duplicateSlot));
	assert(PowerArmorSync::PublishableCapture(inFrame).value() == inFrame);
	assert(PowerArmorSync::PublishableCapture(none).value() == none);

	const auto enter = PowerArmorSync::PlanPieceOperations(none, inFrame);
	assert(enter.size() == 2);
	assert(enter[0].kind == PowerArmorOpKind::kEquip && enter[0].piece.slot == "body" && enter[0].piece.formId == 0x1111U);
	assert(enter[1].kind == PowerArmorOpKind::kEquip && enter[1].piece.slot == "headband" && enter[1].piece.formId == 0x2222U);

	const auto leave = PowerArmorSync::PlanPieceOperations(inFrame, none);
	assert(leave.size() == 2);
	assert(leave[0].kind == PowerArmorOpKind::kUnequip && leave[0].piece.formId == 0x1111U);
	assert(leave[1].kind == PowerArmorOpKind::kUnequip && leave[1].piece.formId == 0x2222U);

	auto swapped = inFrame;
	swapped.pieces[0].formId = 0x3333;
	const auto swap = PowerArmorSync::PlanPieceOperations(inFrame, swapped);
	assert(swap.size() == 2);
	assert(swap[0].kind == PowerArmorOpKind::kUnequip && swap[0].piece.formId == 0x1111U);
	assert(swap[1].kind == PowerArmorOpKind::kEquip && swap[1].piece.formId == 0x3333U);

	auto recipeChanged = inFrame;
	recipeChanged.pieces[0].mods = std::vector<RemotePlayerState::RemoteItemMod>{ { 0x54321, 2, 3 } };
	const auto recipeOps = PowerArmorSync::PlanPieceOperations(inFrame, recipeChanged);
	assert(recipeOps.size() == 1);
	assert(recipeOps[0].kind == PowerArmorOpKind::kEquip);
	assert(recipeOps[0].piece.formId == inFrame.pieces[0].formId);
	assert(recipeOps[0].piece.mods && recipeOps[0].piece.mods->size() == 1);

	auto emptyRecipe = inFrame;
	emptyRecipe.pieces[0].mods = std::vector<RemotePlayerState::RemoteItemMod>{};
	assert(PowerArmorSync::PlanPieceOperations(inFrame, emptyRecipe).empty());
	assert(PowerArmorSync::PlanPieceOperations(emptyRecipe, inFrame).empty());
	assert(inFrame != emptyRecipe);
	assert(recipeChanged != inFrame);

	auto stale = inFrame;
	stale.inFrame = false;
	const auto staleOps = PowerArmorSync::PlanPieceOperations(inFrame, stale);
	assert(staleOps.size() == 2);
	assert(staleOps[0].kind == PowerArmorOpKind::kUnequip);
	assert(staleOps[1].kind == PowerArmorOpKind::kUnequip);
	assert(PowerArmorSync::PlanPieceOperations(none, stale).empty());

	const auto normalizedLeave = PowerArmorSync::NormalizeForApply(stale);
	assert(!normalizedLeave.inFrame);
	assert(normalizedLeave.pieces.empty());
	assert(PowerArmorSync::NormalizeForApply(inFrame) == inFrame);
	const auto staleThenEnter = PowerArmorSync::PlanPieceOperations(normalizedLeave, PowerArmorSync::NormalizeForApply(inFrame));
	assert(staleThenEnter.size() == 2);
	assert(staleThenEnter[0].kind == PowerArmorOpKind::kEquip && staleThenEnter[0].piece.formId == inFrame.pieces[0].formId);
	assert(staleThenEnter[1].kind == PowerArmorOpKind::kEquip && staleThenEnter[1].piece.formId == inFrame.pieces[1].formId);
	assert(PowerArmorSync::PlanPieceOperations(stale, PowerArmorSync::NormalizeForApply(inFrame)).empty());

	const auto leaveOps = PowerArmorSync::PlanPieceOperations(inFrame, normalizedLeave);
	const std::vector<std::uint8_t> leaveDone(leaveOps.size(), 1);
	const auto left = PowerArmorSync::AppliedStateAfterOperations(inFrame, normalizedLeave, leaveOps, leaveDone);
	assert(left == normalizedLeave);
	assert(!left.inFrame && left.pieces.empty());
	const auto reenter = PowerArmorSync::PlanPieceOperations(left, PowerArmorSync::NormalizeForApply(inFrame));
	assert(reenter.size() == 2);
	assert(reenter[0].kind == PowerArmorOpKind::kEquip && reenter[0].piece.formId == 0x1111U);
	assert(reenter[1].kind == PowerArmorOpKind::kEquip && reenter[1].piece.formId == 0x2222U);

	const std::vector<std::uint8_t> partialLeave{ 1, 0 };
	const auto partialLeft = PowerArmorSync::AppliedStateAfterOperations(inFrame, normalizedLeave, leaveOps, partialLeave);
	assert(partialLeft.pieces.size() == 1);
	assert(partialLeft.pieces[0].formId == 0x2222U);
	const auto retryLeave = PowerArmorSync::PlanPieceOperations(partialLeft, normalizedLeave);
	assert(retryLeave.size() == 1);
	assert(retryLeave[0].kind == PowerArmorOpKind::kUnequip && retryLeave[0].piece.formId == 0x2222U);

	const std::vector<std::uint8_t> recipeDone(recipeOps.size(), 1);
	const auto recipeApplied = PowerArmorSync::AppliedStateAfterOperations(inFrame, recipeChanged, recipeOps, recipeDone);
	assert(recipeApplied == recipeChanged);
	assert(recipeApplied.pieces.size() == inFrame.pieces.size());

	auto sameFrameOtherForm = inFrame;
	sameFrameOtherForm.frameBaseFormId = 0x0020B0FF;
	assert(PowerArmorSync::PlanPieceOperations(inFrame, sameFrameOtherForm).empty());
	assert(inFrame != sameFrameOtherForm);

	return 0;
}
