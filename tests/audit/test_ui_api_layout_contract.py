import hashlib
import re
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
UI_HEADER = ROOT / "CommonwealthOnlineUI" / "include" / "CommonwealthOnlineUI_API.h"
PLUGIN_HEADER = ROOT / "CO-BETA" / "plugin" / "include" / "CommonwealthOnlineUI_API.h"
UI_EXPORTS = ROOT / "CommonwealthOnlineUI" / "src" / "PluginAPI.cpp"

SHAPE_PINS = {
    "V1": (0x7C3A1E01, "c9aba93482ad1048"),
    "V2": (0x51D90B02, "06808a0f9007e734"),
    "V3": (0x2E86F403, "a93d34a8f57026d2"),
    "V4": (0x9A4C7204, "fbd5a2024650880e"),
    "V5": (0x63F1D805, "d1c9da29c14acc31"),
    "V6": (0x0DB52906, "8b305849acc37b1c"),
    "V7": (0x3D82E907, "1c34d6e2b17a0172"),
    "V8": (0x46A2E708, "5988e619a2fa7d3c"),
    "V9": (0xC51F3A09, "bb91c681c4099baf"),
    "V10": (0xE17A9C0A, "7aee6a954b5308cd"),
    "V11": (0x2D7F1B0B, "c8ca3ecbfd94e89f"),
    "V12": (0x7A4B5C0C, "7aa426077347f200"),
    "V13": (0xE6A14D0D, "f9cf0cdc66bfe993"),
    "V14": (0x5E1D0C0E, "de9c919ad2a569ab"),
    "V15": (0x8B3F2E0F, "788478b4764d58b5"),
    "V16": (0xD3B8F411, "d291a8ca5f305303"),
}


def read(path: Path) -> str:
    return path.read_text(encoding="utf-8").replace("\r\n", "\n")


def namespace_body(text: str) -> str:
    match = re.search(r"^namespace COMMONWEALTH_ONLINE_UI_API\n\{\n(.*?)^\}\n", text, re.S | re.M)
    assert match, "COMMONWEALTH_ONLINE_UI_API namespace is missing"
    return match.group(1)


def enumerators(body: str) -> list[str]:
    match = re.search(r"enum class InterfaceVersion : std::uint8_t\n\t\{\n(.*?)\n\t\};", body, re.S)
    assert match, "InterfaceVersion enum is missing"
    names = re.findall(r"\b(V\d+)\s*=\s*(\d+)", match.group(1))
    assert names, "InterfaceVersion has no enumerators"
    for name, value in names:
        assert name == f"V{value}", f"{name} must equal {name[1:]}"
    return [name for name, _ in names]


def declaration(header: str, name: str) -> str | None:
    match = re.search(rf"^\tstruct {name}\n\t\{{\n.*?^\t\}};\n", header, re.S | re.M)
    if not match:
        match = re.search(rf"^\tusing {name} = [^;]*;\n", header, re.S | re.M)
    return match.group(0) if match else None


def shape_hash(header: str, struct: str) -> str:
    seen, pending, parts = set(), [struct], []
    while pending:
        name = pending.pop(0)
        decl = declaration(header, name)
        if name in seen or decl is None:
            continue
        seen.add(name)
        parts.append(decl)
        pending.extend(sorted(set(re.findall(r"\b[A-Z]\w+\b", decl)) - seen))
    return hashlib.sha256("".join(parts).encode()).hexdigest()[:16]


def test_header_copies_match():
    assert read(UI_HEADER) == read(PLUGIN_HEADER), (
        "CO-BETA/plugin/include/CommonwealthOnlineUI_API.h and CommonwealthOnlineUI/include/CommonwealthOnlineUI_API.h differ; "
        "copy one over the other"
    )
    assert namespace_body(read(UI_HEADER)) == namespace_body(read(PLUGIN_HEADER))


def test_each_version_has_one_struct_and_one_layout():
    body = namespace_body(read(UI_HEADER))
    layouts = {}
    for name in enumerators(body):
        number = name[1:]
        struct = f"IVCommonwealthOnlineUI{number}"
        assert len(re.findall(rf"^\tstruct {struct}\n", body, re.M)) == 1, f"{name} needs exactly one struct {struct}"
        maps = re.findall(
            rf"struct InterfaceVersionMap<(\w+)>\n\t\{{\n\t\tstatic constexpr InterfaceVersion version = InterfaceVersion::{name};\n"
            rf"\t\tstatic constexpr std::uint32_t\s+layout = (0x[0-9A-Fa-f]{{8}});\n\t\}};",
            body,
        )
        assert len(maps) == 1, f"{name} needs exactly one InterfaceVersionMap with a hex layout"
        assert maps[0][0] == struct, f"{name} maps to {maps[0][0]}, expected {struct}"
        layouts[name] = int(maps[0][1], 16)
    structs = re.findall(r"^\tstruct (IVCommonwealthOnlineUI\d+)\n", body, re.M)
    assert len(structs) == len(layouts), f"interface structs {structs} do not match enumerators {sorted(layouts)}"
    assert len(re.findall(r"struct InterfaceVersionMap<", body)) == len(layouts)
    assert len(set(layouts.values())) == len(layouts), f"layout constants must be unique: {layouts}"
    assert sorted(SHAPE_PINS) == sorted(layouts), f"SHAPE_PINS must list exactly {sorted(layouts)}"
    for name, layout in layouts.items():
        pinned_layout, pinned_shape = SHAPE_PINS[name]
        actual = shape_hash(read(UI_HEADER), f"IVCommonwealthOnlineUI{name[1:]}")
        assert actual == pinned_shape and layout == pinned_layout, (
            f"{name} changed (shape {actual}, layout {layout:#010x}; pinned {pinned_shape}, {pinned_layout:#010x}). "
            "Give the interface a new layout constant, then update both values in SHAPE_PINS"
        )


def test_plugin_requests_only_the_checked_export():
    body = namespace_body(read(PLUGIN_HEADER))
    lookups = re.findall(r'GetProcAddress\(\w+, "(\w+)"\)', body)
    assert lookups == ["RequestPluginAPIChecked"], f"plugin must resolve only RequestPluginAPIChecked, found {lookups}"
    request = re.search(r"inline T\* RequestPluginAPI\(\)\n\t\{\n(.*?)\n\t\}", body, re.S)
    assert request, "RequestPluginAPI<T>() is missing"
    assert "requestPluginAPI(InterfaceVersionMap<T>::version, InterfaceVersionMap<T>::layout)" in request.group(1)
    assert "OnInterfaceRefused(" in request.group(1)
    accept = re.search(r"inline T\* AcceptInterface\(T\* a_table, std::uint32_t a_layout\)\n\t\{\n(.*?)\n\t\}", body, re.S)
    assert accept and "a_layout == InterfaceVersionMap<T>::layout ? a_table : nullptr" in accept.group(1)


def test_ui_checked_export_refuses_layout_mismatch():
    text = read(UI_EXPORTS)
    names = enumerators(namespace_body(read(UI_HEADER)))
    visit = re.search(r"void\* VisitInterface\(.*?\n\t\}\n", text, re.S)
    assert visit, "VisitInterface is missing from PluginAPI.cpp"
    for name in names:
        number = name[1:]
        assert f"case InterfaceVersion::{name}:\n\t\t\treturn a_visit(g_interfaceV{number});" in visit.group(0), (
            f"VisitInterface does not route {name} to g_interfaceV{number}"
        )
    assert re.search(r'extern "C" __declspec\(dllexport\) void\* F4SEAPI RequestPluginAPI\(\n', text), (
        "the legacy RequestPluginAPI export must stay for older gameplay DLLs"
    )
    checked = re.search(
        r'extern "C" __declspec\(dllexport\) void\* F4SEAPI RequestPluginAPIChecked\(\n.*?\n\}\n', text, re.S
    )
    assert checked, "RequestPluginAPIChecked export is missing"
    checked_body = checked.group(0)
    assert "VisitInterface(a_interfaceVersion" in checked_body
    assert "AcceptInterface(&a_table, a_layout)" in checked_body
    assert re.search(r"return api;\n\t\t\}\n\t\tREX::CRITICAL\(.*?return nullptr;", checked_body, re.S), (
        "a refused layout must log and return nullptr"
    )
    assert "return &a_table" not in checked_body, "the checked export must never hand out a table without the layout gate"


if __name__ == "__main__":
    test_header_copies_match()
    test_each_version_has_one_struct_and_one_layout()
    test_plugin_requests_only_the_checked_export()
    test_ui_checked_export_refuses_layout_mismatch()
    print("UI API layout contract passed")
