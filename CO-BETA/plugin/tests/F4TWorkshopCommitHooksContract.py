from pathlib import Path
import re


root = Path(__file__).resolve().parents[3]
plugin = root / "CO-BETA/plugin"
header = (plugin / "include/F4TWorkshopCommitHooks.h").read_text(encoding="utf-8")
observer = (plugin / "include/F4TWorkshopObserver.h").read_text(encoding="utf-8")
hooks = (plugin / "src/F4TWorkshopCommitHooks.cpp").read_text(encoding="utf-8")
main_cpp = (plugin / "src/main.cpp").read_text(encoding="utf-8")
workflow = (root / ".github/workflows/co-beta-ci.yml").read_text(encoding="utf-8")
place_note = "source:RE-Work/subsystems/commonwealth-online-re/workshop-followup-co185-place-rotation/README.md"
commit_note = "source:RE-Work/subsystems/commonwealth-online-re/workshop-commit-co185/README.md"
scrap_note = "source:RE-Work/subsystems/commonwealth-online-re/workshop-followup-co185-scrap/README.md"
rotate_note = "source:RE-Work/subsystems/commonwealth-online-re/workshop-rotate-commit-co185/README.md"
release_note = "source:RE-Work/subsystems/commonwealth-online-re/co245-workshop/README.md"

PLACE_OG = [0x48, 0x8B, 0xC4, 0x48, 0x89, 0x48, 0x08, 0x55, 0x53, 0x41, 0x54, 0x41, 0x56, 0x48, 0x8D, 0xA8, 0x38, 0xFE, 0xFF, 0xFF]
PLACE_NG_AE = [0x48, 0x8B, 0xC4, 0x48, 0x89, 0x48, 0x08, 0x55, 0x53, 0x41, 0x55, 0x41, 0x56, 0x48, 0x8D, 0xA8, 0xD8, 0xFD, 0xFF, 0xFF]


def function_body(name: str) -> str:
    match = re.search(rf"\n(\t+)[^\n]*\b{name}\([^\n]*\)\n\1\{{\n(.*?)\n\1\}}\n", hooks, re.DOTALL)
    assert match is not None, name
    return match.group(2)


def prologue(name: str, length: int = 20) -> list[int]:
    match = re.search(rf"constexpr std::array<std::uint8_t, (\d+)> {name}\{{(.*?)\}};", hooks, re.DOTALL)
    assert match is not None, name
    values = [int(value, 16) for value in re.findall(r"0x([0-9A-Fa-f]{2})", match.group(2))]
    assert int(match.group(1)) == length and len(values) == length, name
    return values


assert "\tbool Install();\n" in header
assert "kEnableWorkshopCommitHooks" not in header

assert "kCoTestFeatures" not in hooks and '#include "F4TTestFeatures.h"' not in hooks

install = function_body("Install")
assert install.lstrip().startswith("const auto target = VerifiedTarget();"), install[:160]
for resolver in ("VerifiedTarget()", "InstallPlacement(", "InstallUpdateActiveEdit(", "InstallScrapAccept()", "InstallConfirmCall()"):
    assert hooks.count(resolver) == install.count(resolver) + 1, f"{resolver} must be called only from Install"

assert prologue("kPlacePrologueOg") == PLACE_OG
assert prologue("kPlacePrologueNgAe") == PLACE_NG_AE
assert PLACE_OG[13:16] == PLACE_NG_AE[13:16] == [0x48, 0x8D, 0xA8]
assert "a_target == F4T::ClientCompatibility::BuildTarget::OG ? REL::ID{ 1058211 } : REL::ID{ 2195155 }" in hooks
assert "REL::ID{ 1124584 }" in hooks
assert "constexpr std::size_t kScrapRefHandleOffset = 0x10;" in hooks

signature = re.search(r"void ScrapAcceptHook\(([^()]*)\)", hooks)
assert signature is not None and signature.group(1) == "void* a_callback", signature
assert "using ScrapAcceptFn = void (*)(void*);" in hooks
assert "constexpr std::size_t kScrapAcceptSlot = 1;" in hooks
# source:RE-Work/subsystems/commonwealth-online-re/workshop-followup-co185-scrap/README.md (Address Library vtable id 1124584)
assert re.search(r"REL::ID\{ 1124584 \},\n\t\t\t\tkScrapAcceptSlot,\n\t\t\t\t// source:[^\n]*\n\t\t\t\tF4T::ExecutingRuntime::RvaFor\(g_target, [^\n]*\),\n\t\t\t\tREX::UNRESTRICTED_CAST<std::uintptr_t>\(&ScrapAcceptHook\),\n\t\t\t\tg_scrapAcceptOriginal,", hooks)
assert hooks.count("vtable.write_vfunc(a_slot, a_replacement)") == 1
assert "VtableGuard::VerifySlot(request)" in hooks

scrap = function_body("ScrapAcceptHook")
assert "return" not in scrap
assert scrap.rstrip().endswith("g_inScrapAccept = true;\n\t\t\treinterpret_cast<ScrapAcceptFn>(g_scrapAcceptOriginal)(a_callback);\n\t\t\tg_inScrapAccept = false;")
assert hooks.count("g_inScrapAccept = true;") == 1 and hooks.count("g_inScrapAccept = false;") == 1
place_early = function_body("PlaceHook")
assert place_early.index("g_release.OnPlaceAttempt(handle->get_handle())") < place_early.index("if (g_inScrapAccept) {") < place_early.index("DescribeHeld(*refr)")

place = function_body("PlaceHook")
assert place.count("return ") == 2 and place.startswith("\t\t\tif (!g_placeHook.hook) {\n\t\t\t\treturn false;\n\t\t\t}\n")
assert place.rstrip().endswith("return g_placeHook.hook->original<PlaceFn>()(a_context);")

assert "a_original = request.slotValue;\n\t\t\tvtable.write_vfunc(a_slot, a_replacement);" in hooks, "the original must be stored before the slot is written"
assert "#include" not in observer.replace("#include <cstdint>", "").replace("#include <optional>", ""), "the observer header must stay engine-free"
assert "constexpr std::int32_t kScrapCancelId = 1;" in observer

# source:RE-Work/subsystems/commonwealth-online-re/workshop-commit-co185 (comment 16670, WS-1)
UPDATE_OG = [0x88, 0x54, 0x24, 0x10, 0x48, 0x89, 0x4C, 0x24, 0x08, 0x55, 0x53, 0x41,
             0x55, 0x41, 0x56, 0x48, 0x8D, 0xAC, 0x24, 0x78, 0xFD, 0xFF, 0xFF]
# source:RE-Work/subsystems/commonwealth-online-re/workshop-commit-co185 (comment 16670, WS-1)
UPDATE_NG_AE = [0x88, 0x54, 0x24, 0x10, 0x48, 0x89, 0x4C, 0x24, 0x08, 0x55, 0x56, 0x41,
                0x55, 0x41, 0x56, 0x41, 0x57, 0x48, 0x8D, 0xAC, 0x24, 0x00, 0xFE, 0xFF, 0xFF]
assert prologue("kUpdateActiveEditPrologueOg", 23) == UPDATE_OG
assert prologue("kUpdateActiveEditPrologueNg", 25) == UPDATE_NG_AE
assert "constexpr auto& kUpdateActiveEditPrologueAe = kUpdateActiveEditPrologueNg;" in hooks
assert hooks.count("comment 16670, WS-1") == 3
update_install = function_body("InstallUpdateActiveEdit")
assert update_install.index("REL::ID{ 69261 }") < update_install.index("RegisterForItemGrabbed(&g_grabSink)") < update_install.index("InstallInline(")
assert "InstallInline(updateId, prologue," in update_install
for target in ("Og", "Ng", "Ae"):
    assert f"std::span<const std::uint8_t>{{ kUpdateActiveEditPrologue{target} }}" in update_install
inline = (plugin / "src/F4TInlineHook.cpp").read_text(encoding="utf-8")
assert inline.index("InlineHookGuard::VerifyPrologue(") < inline.index("safetyhook::InlineHook::create(")
assert "const auto prologueLength = a_request.expectedPrologue.size();" in inline
assert "a_request.expectedPrologue, BytesAt(a_request.target, prologueLength)" in inline
assert "if (a_result.verdict == Verdict::kOk) {\n\t\t\t\tauto created = safetyhook::InlineHook::create(" in inline
assert inline.index("safetyhook::InlineHook::StartDisabled") < inline.index("a_result.hook.emplace(") < inline.index("a_result.hook->enable()")
assert "REL::ID{ 69261 } : REL::ID{ 2195152 }" in update_install
assert "UnregisterForItemGrabbed(&g_grabSink)" in update_install

update = function_body("UpdateActiveEditHook")
assert update.startswith("\t\t\tif (!g_updateActiveEditHook.hook) {\n\t\t\t\treturn;\n\t\t\t}\n") and update.count("return") == 1
assert update.index("DescribeHeld(*refr)") < update.index("g_grabSeen = false;") < update.index("original<UpdateActiveEditFn>()(a_context, a_flag);")
assert update.index("original<UpdateActiveEditFn>()(a_context, a_flag);") < update.index("!g_grabSeen") < update.index("g_release.OnReleaseArm(heldHandle, IsSpline(*refr))") < update.index('"move release: {}"')
assert hooks.count('"move release: {}"') == 1
assert hooks.count("g_grabSeen = true;") == 1
assert place.index("g_release.OnPlaceAttempt(handle->get_handle())") < place.index("DescribeHeld(*refr)")
assert hooks.count("g_release.OnPlaceAttempt(") == 1 and hooks.count("g_release.OnReleaseArm(") == 1

actor_angle = function_body("ActorAngleFlag")
# source:RE-Work/subsystems/commonwealth-online-re/workshop-commit-co185 (comment 16670, WS-2)
assert "static REL::Relocation<Predicate> predicate{ REL::ID{ 1200091 } };" in actor_angle
assert "return predicate(&a_ref);" in actor_angle
assert actor_angle.index("a_ref.GetFormFlags()") < actor_angle.index("BuildTarget::OG") < actor_angle.index("return predicate(&a_ref);")
assert "return std::nullopt;" not in actor_angle
assert "comment 16670, WS-2" in actor_angle
assert "constexpr std::uint32_t kActorAngleBit = 9;" in hooks
assert "base->GetFormType() == RE::ENUM_FORM_ID::kBNDS" in function_body("IsSpline")
assert "a_ref.GetFormType() == RE::ENUM_FORM_ID::kACHR" in function_body("DescribeHeld")
assert "non-actor matrix is Rz(z)*Ry(y)*Rx(x), right-handed R(+angle); axis names are a convention" in hooks

assert "constexpr std::size_t kConfirmCallSlot = 1;" in hooks
# source:RE-Work/subsystems/commonwealth-online-re/workshop-followup-co185-scrap/README.md (ExamineConfirmMenu primary vtable address id 289899)
assert re.search(r"REL::ID\{ 289899 \},\n\t\t\t\tkConfirmCallSlot,\n\t\t\t\t// source:[^\n]*\n\t\t\t\tF4T::ExecutingRuntime::RvaFor\(g_target, [^\n]*\),\n\t\t\t\tREX::UNRESTRICTED_CAST<std::uintptr_t>\(&ConfirmCallHook\),\n\t\t\t\tg_confirmCallOriginal,", hooks)
confirm = function_body("ConfirmCallHook")
assert "return" not in confirm
assert "a_params.userData" in confirm
assert confirm.index("g_scrap.OnConfirmCall(userData);") < confirm.index("reinterpret_cast<ConfirmCallFn>(g_confirmCallOriginal)(a_menu, a_params);")
assert confirm.rstrip().endswith("reinterpret_cast<ConfirmCallFn>(g_confirmCallOriginal)(a_menu, a_params);")
assert "scrap accept" not in confirm and "OnAccept" not in confirm
assert hooks.count("g_scrap.OnAccept()") == 1 and "g_scrap.OnAccept()" in scrap
assert hooks.count('"scrap accept: ref handle {:#010x}, decided by {}"') == 1 and '"scrap accept: ref handle {:#010x}, decided by {}"' in scrap

assert "Networking" not in hooks and "Packet" not in hooks

for note in (place_note, commit_note, scrap_note, rotate_note, release_note):
    assert f"// {note}" in hooks, note

assert '#include "F4TWorkshopCommitHooks.h"' in main_cpp
polling = re.search(r"void\s+StartPlayerPositionPolling\(\)\s*\{(.*?)\n\t\}", main_cpp, re.DOTALL)
assert polling is not None, "StartPlayerPositionPolling not found in main.cpp"
polling_body = polling.group(1)
assert polling_body.index("F4T::ProxyPuppet::Install();") < polling_body.index("F4T::WorkshopCommitHooks::Install();")

assert workflow.count("python3 plugin/tests/F4TWorkshopCommitHooksContract.py") == 1

workshop_src = (Path(__file__).resolve().parents[1] / "src/F4TWorkshopCommitHooks.cpp").read_text(encoding="utf-8")
for fn in ("PlaceHook", "UpdateActiveEditHook", "ScrapAcceptHook", "ConfirmCallHook"):
    body = workshop_src[workshop_src.index(fn + "("):]
    body = body.split("\n\t\t}\n")[0]
    assert "catch (...)" in body, fn
    assert body.count("catch (...)") >= (2 if fn == "UpdateActiveEditHook" else 1), fn

print("F4TWorkshopCommitHooksContract: all checks passed.")
