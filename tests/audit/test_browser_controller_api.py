from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]
HEADER = ROOT / "CommonwealthOnlineUI/include/PrismaUI_F4_API.h"
CLANG = shutil.which("clang++") or "C:/Program Files/LLVM/bin/clang++.exe"


def main():
    header = Path(sys.argv[1]) if len(sys.argv) > 1 else HEADER
    source = "\n".join(('#include "' + header.as_posix() + '"', "", "#include <type_traits>", "")) + r'''using namespace PRISMA_UI_API;
static_assert(static_cast<unsigned>(InterfaceVersion::V9) == 136);
static_assert(static_cast<unsigned>(InterfaceVersion::V10) == 137);
static_assert(static_cast<unsigned>(InterfaceVersion::V11) == 138);
static_assert(static_cast<unsigned>(InterfaceVersion::V12) == 139);
static_assert(static_cast<unsigned>(CursorPolicy::Hidden) == 1);
static_assert(std::is_same_v<decltype(&IVPrismaUI12::GetControllerActionBridgeState),
    ControllerActionBridgeState (IVPrismaUI12::*)(PrismaView) noexcept>);
'''
    with tempfile.TemporaryDirectory(prefix="co-controller-api-") as temp:
        cpp = Path(temp) / "api.cpp"
        cpp.write_text(source, encoding="utf-8")
        subprocess.run([
            CLANG, "-std=c++23", "-U_WIN32",
            "-fsyntax-only", str(cpp),
        ], check=True)
    print("Browser canonical controller API contract passed")


if __name__ == "__main__":
    main()
