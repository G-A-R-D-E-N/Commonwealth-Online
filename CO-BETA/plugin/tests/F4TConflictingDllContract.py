from pathlib import Path

PLUGIN_DIR = Path(__file__).resolve().parents[1]
select_h = (PLUGIN_DIR / "src/F4TRuntimeSelect.h").read_text(encoding="utf-8")
main_cpp = (PLUGIN_DIR / "src/main.cpp").read_text(encoding="utf-8")


def body(text, signature):
    start = text.index(signature)
    open_brace = text.index("{", start)
    depth = 0
    for index in range(open_brace, len(text)):
        if text[index] == "{":
            depth += 1
        elif text[index] == "}":
            depth -= 1
            if depth == 0:
                return text[open_brace:index + 1]
    raise AssertionError(f"unbalanced body for {signature}")


find = body(select_h, "FindConflictingDlls(const std::filesystem::path& a_directory, std::wstring_view a_ownFileName)")
assert "std::filesystem::directory_iterator it(a_directory, ec)" in find and "it.increment(ec)" in find, "the scan must enumerate the folder without throwing"
assert 'key.starts_with(L"commonwealthonline") && key.ends_with(L".dll")' in find, "every CommonwealthOnline*.dll must be checked, case-insensitively"
assert "key != own" in find, "the plugin's own file is never a conflict"
assert 'key != L"commonwealthonline.dll"' in find and 'key != L"commonwealthonlineprobe.dll"' in find and 'key != L"commonwealthonlinedebug.dll"' in find, "the canonical DLL, the RE probe and the dev debug plugin are not conflicts"
assert "CommonwealthOnline-NG.dll" not in select_h and "LegacyDllsPresent" not in select_h, "the fixed legacy name list is gone"

wrapper = body(select_h, "ConflictingDlls() noexcept")
assert "return FindConflictingDlls(self.parent_path(), self.filename().wstring());" in wrapper, "the wrapper scans the plugin's own folder and excludes its own file name"

assert "const auto conflicts = F4T::RuntimeSelect::ConflictingDlls(); !conflicts.empty()" in main_cpp, "plugin load must refuse when another CommonwealthOnline DLL is present"
load = main_cpp[main_cpp.index("const auto conflicts = F4T::RuntimeSelect::ConflictingDlls()"):]
load = load[:load.index("return false;")]
assert "names" in load and "REX::WARN(" in load and "::MessageBoxW(nullptr, text.c_str()" in load, "the log and the message box must name the files found"
assert main_cpp.index("ConflictingDlls()") < main_cpp.index("F4T::RuntimeSelect::RegisterClientTarget();"), "the refusal must run before any other load step"
print("conflicting DLL contract ok")
