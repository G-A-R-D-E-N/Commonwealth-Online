from pathlib import Path
import argparse
import subprocess
import tempfile


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--compiler", default="clang++")
    parser.add_argument("--json-include", required=True)
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    source = (root / "src/F4TCharacterCreatorCommands.cpp").read_text(encoding="utf-8")
    request = source[source.index("\t\tstruct CreatorRequest"):source.index("\n\t\tstd::uint32_t ReadMovieBoneId")]
    dispatch = source[source.index("\t\tvoid DispatchCreatorAction"):source.index("\n\t}\n\n\tvoid HandleCreatorAction")]
    handle = source[source.index("\tvoid HandleCreatorAction"):source.rindex("\n}")]
    harness = "\n" + "\n".join([
        "#include \"F4TCharacterCreatorCommands.h\"",
        "#include <nlohmann/json.hpp>",
        "#include <atomic>",
        "#include <cassert>",
        "#include <format>",
        "#include <functional>",
        "#include <cstdio>",
        "#include <string>",
    ]) + r'''
using namespace F4T::CharacterCreator::Commands;
struct State { bool menuOpen = true; std::atomic<std::uint64_t> generation{1}; } g_state;
int mutations = 0;
int reads = 0;
void LogWarning(std::string_view) {}
void PushState() { ++reads; }
void SendMovieInput(std::string_view, bool) { ++mutations; }
void SendCreatorOptions(std::uint32_t, std::uint32_t, bool) { ++reads; }
void SendCreatorPresets() { ++reads; }
void LoadCreatorPreset(std::string_view) { ++mutations; }
void SendCreatorAxes() { ++reads; }
void SetMovieBoneRegion(std::uint32_t) { ++mutations; }
namespace F4SE {
struct Tasks { void AddTask(std::function<void()> task) const { pending = task; } mutable std::function<void()> pending; } tasks;
bool available = true;
const Tasks* GetTaskInterface() { return available ? &tasks : nullptr; }
}
'''
    harness += request + "\nvoid InvokeCodeObject(const CreatorRequest&) { ++mutations; }\n" + dispatch + handle
    harness += r'''
void Run(const char* json) {
    HandleCreatorAction(json);
    if (F4SE::tasks.pending) {
        auto task = std::move(F4SE::tasks.pending);
        F4SE::tasks.pending = {};
        task();
    }
}
int main() try {
    for (const auto* json : {"[]", "null", R"({"action":7})", R"({"action":"input","control":2,"generation":1})", R"({"action":"input","keyUp":3,"control":"Accept","generation":1})", R"({"action":"setSculptMode","flag":3,"generation":1})", R"({"action":"loadPreset","path":2,"generation":1})"}) {
        Run(json);
        assert(mutations == 0 && reads == 0);
    }
    Run(R"({"action":"getOptions","feature":4294967296,"generation":1})");
    assert(reads == 0);
    Run(R"({"action":"getState"})");
    assert(reads == 1);
    Run(R"({"action":"confirm","generation":1})");
    assert(mutations == 1);
    HandleCreatorAction(R"({"action":"confirm","generation":1})");
    ++g_state.generation;
    auto stale = std::move(F4SE::tasks.pending);
    stale();
    assert(mutations == 1);
    Run(R"({"action":"confirm","generation":1})");
    assert(mutations == 1);
    Run(R"({"action":"getAxes","generation":1})");
    Run(R"({"action":"getState","generation":1})");
    assert(reads == 1);
    for (const auto* json : {R"({"action":"confirm"})", R"({"action":"confirm","generation":"2"})", R"({"action":"confirm","generation":-1})", R"({"action":"confirm","generation":2.5})", R"({"action":"confirm","generation":18446744073709551615})"}) {
        Run(json);
        assert(mutations == 1);
    }
    Run(R"({"action":"getState"})");
    assert(reads == 2);
    Run(R"({"action":"getAxes","generation":2})");
    assert(reads == 3);
    Run(R"({"action":"confirm","generation":2})");
    assert(mutations == 2);
    g_state.menuOpen = false;
    Run(R"({"action":"input","control":"Accept","generation":2})");
    assert(mutations == 2);
    g_state.menuOpen = true;
    F4SE::available = false;
    Run(R"({"action":"confirm","generation":2})");
    assert(mutations == 2);
}
catch (const std::exception& ex) {
    std::fprintf(stderr, "Dispatch escaped JSON boundary: %s\n", ex.what());
    return 1;
}
'''
    with tempfile.TemporaryDirectory(prefix="co-creator-dispatch-") as temp:
        cpp = Path(temp) / "dispatch.cpp"
        binary = Path(temp) / "dispatch.exe"
        cpp.write_text(harness, encoding="utf-8")
        subprocess.run([args.compiler, "-std=c++20", "-UNDEBUG", "-Wall", "-Wextra", "-Werror", "-I" + str(root / "include"), "-I" + args.json_include, str(cpp), "-o", str(binary)], check=True)
        subprocess.run([str(binary)], check=True)
    print("Character creator dispatch boundary checks passed")


if __name__ == "__main__":
    main()
