# TODO-verify(#407)
import argparse
import importlib.util
from pathlib import Path
import re
import struct
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[3]
SOURCE = ROOT / "CO-BETA/plugin/src/F4TProbeSites.cpp"
SCRIPT = ROOT / "scripts/probe-site-bytes.py"
RUNTIME_CODES = {"Og": "163", "Ng": "984", "Ae": "240"}
ID_PATTERN = re.compile(r"constexpr std::uint16_t (\w+) = (\d+);")
ROW_PATTERN = re.compile(r"\{\s*(\w+),\s*Runtime::(Og|Ng|Ae),\s*(0x[0-9A-Fa-f]+),\s*\{([^}]*)\},\s*&\w+\s*\}")
BYTE_PATTERN = re.compile(r"0x[0-9A-Fa-f]{2}")
OUTPUT_PATTERN = re.compile(r"^(\d+) (0x[0-9A-F]+) file_offset=0x[0-9A-F]+: (.*)$", re.MULTILINE)
BRANCH_TARGET = re.compile(r"0x[0-9a-f]+")
HOOK_JUMP_SIZE = 5
SWEEP_RADIUS = 0x10000


def load_script():
    spec = importlib.util.spec_from_file_location("probe_site_bytes", SCRIPT)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def parse_rows(text):
    ids = {name: int(value) for name, value in ID_PATTERN.findall(text)}
    start = text.index("kSites = std::to_array<Site>(")
    block = text[start:text.index("std::string_view RuntimeName", start)]
    rows = []
    for name, runtime, rva, body in ROW_PATTERN.findall(block):
        values = [int(value, 16) for value in BYTE_PATTERN.findall(body)]
        rows.append((ids[name], runtime, int(rva, 16), values))
    return rows, block.count("Runtime::")


# RVA data source:scripts/probe-site-bytes.py
def raw_offset(sections, rva, length):
    for virtual_address, _, raw_pointer, raw_size, _ in sections:
        if virtual_address <= rva and rva + length <= virtual_address + raw_size:
            return raw_pointer + rva - virtual_address
    raise ValueError(f"0x{rva:X} has no raw data")


def function_ranges(data, sections):
    pe = struct.unpack_from("<I", data, 0x3C)[0]
    table_rva, table_size = struct.unpack_from("<II", data, pe + 24 + 112 + 3 * 8)
    table = raw_offset(sections, table_rva, table_size)
    return [struct.unpack_from("<II", data, table + index) for index in range(0, table_size, 12)]


def displaced_branch_problems(module, rows):
    try:
        import capstone
    except ImportError:
        print("SKIP displaced branch check: capstone not installed")
        return []
    decoder = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)
    problems = []
    for runtime, code in RUNTIME_CODES.items():
        data = (module.EXE_DIR / module.EXE_NAMES[code]).read_bytes()
        _, sections = module.read_sections(data)
        functions = function_ranges(data, sections)
        for probe, row_runtime, rva, _ in rows:
            if row_runtime != runtime:
                continue
            start = raw_offset(sections, rva, 32)
            length = 0
            for _, size, _, _ in decoder.disasm_lite(data[start:start + 32], rva):
                length += size
                if length >= HOOK_JUMP_SIZE:
                    break
            for begin, end in functions:
                if abs(begin - rva) > SWEEP_RADIUS:
                    continue
                body = raw_offset(sections, begin, end - begin)
                for address, _, mnemonic, operands in decoder.disasm_lite(data[body:body + end - begin], begin):
                    if mnemonic[0] != "j" and mnemonic != "call":
                        continue
                    if BRANCH_TARGET.fullmatch(operands) and rva < int(operands, 16) < rva + length:
                        problems.append(
                            f"probe {probe} {runtime} 0x{rva:X}: {mnemonic} at 0x{address:X} "
                            f"lands inside the {length} patched bytes"
                        )
    return problems


def main(argv=None):
    parser = argparse.ArgumentParser(description="Check kSites rows against the unpacked runtime executables.")
    parser.add_argument("--source", type=Path, default=SOURCE, help="F4TProbeSites.cpp to check")
    args = parser.parse_args(argv)

    rows, mentions = parse_rows(args.source.read_text(encoding="utf-8"))
    if not rows or len(rows) != mentions:
        print(f"FAIL: parsed {len(rows)} kSites rows from {mentions} Runtime:: entries", file=sys.stderr)
        return 1

    problems = []
    seen = set()
    for probe, runtime, rva, values in rows:
        if rva == 0:
            problems.append(f"probe {probe} {runtime} has a zero RVA")
        if (probe, runtime) in seen:
            problems.append(f"probe {probe} is listed twice for {runtime}")
        seen.add((probe, runtime))
        if len(values) != 16:
            problems.append(f"probe {probe} {runtime} 0x{rva:X} lists {len(values)} bytes, expected 16")
    if problems:
        for problem in problems:
            print(f"FAIL: {problem}", file=sys.stderr)
        return 1

    module = load_script()
    needed = {module.EXE_DIR / module.EXE_NAMES[RUNTIME_CODES[runtime]] for _, runtime, _, _ in rows}
    absent = sorted(str(path) for path in needed if not path.exists())
    if absent:
        print("SKIP probe sites contract: executables not found: " + ", ".join(absent))
        return 0

    requests = [f"{RUNTIME_CODES[runtime]}:0x{rva:X}" for _, runtime, rva, _ in rows]
    result = subprocess.run(
        [sys.executable, "-I", "-X", "utf8", str(SCRIPT), *requests],
        capture_output=True,
        text=True,
    )
    if result.returncode != 0:
        problems.append(f"probe-site-bytes.py exited {result.returncode}: {result.stderr.strip()}")
    observed = {
        (code, int(rva_text, 16)): [int(value, 16) for value in BYTE_PATTERN.findall(body)]
        for code, rva_text, body in OUTPUT_PATTERN.findall(result.stdout)
    }
    for probe, runtime, rva, values in rows:
        key = (RUNTIME_CODES[runtime], rva)
        if key not in observed:
            problems.append(f"probe {probe} {runtime} 0x{rva:X} has no bytes from probe-site-bytes.py")
        elif observed[key] != values:
            problems.append(
                f"probe {probe} {runtime} 0x{rva:X}: source bytes {values} differ from executable bytes {observed[key]}"
            )

    problems.extend(displaced_branch_problems(module, rows))

    for problem in problems:
        print(f"FAIL: {problem}", file=sys.stderr)
    if problems:
        return 1
    print(f"probe sites contract PASS ({len(rows)} rows)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
