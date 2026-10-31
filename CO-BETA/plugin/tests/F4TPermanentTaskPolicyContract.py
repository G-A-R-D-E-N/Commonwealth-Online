import os
import re
from collections import defaultdict
from pathlib import Path

root = Path(__file__).resolve().parents[3]
header = "F4TF4SEPermanentTask.h"
policy = "F4TF4SECapabilityPolicy.cpp"
skipped_dirs = {".git", "ThirdParty", "build", "lib", "node_modules"}


def repo_files(keep):
    for directory, dirs, names in os.walk(root):
        dirs[:] = [name for name in dirs if name not in skipped_dirs]
        for name in names:
            if keep(name):
                yield Path(directory, name)


sources = {path: path.read_text(encoding="utf-8", errors="replace")
           for path in repo_files(lambda name: name.endswith((".h", ".cpp")))}
by_name = defaultdict(list)
for path in sources:
    by_name[path.name].append(path)
includers = defaultdict(set)
for path, text in sources.items():
    for included in re.findall(r'^\s*#\s*include\s*"([^"]+)"', text, re.M):
        for target in by_name[Path(included).name]:
            includers[target].add(path)

assert by_name[header], f"{header} not found under {root}"
reached = set()
pending = list(by_name[header])
while pending:
    for parent in includers[pending.pop()]:
        if parent not in reached:
            reached.add(parent)
            pending.append(parent)
needs_policy = {path.name for path in reached if path.suffix == ".cpp"}

blocks = defaultdict(list)
block_start = re.compile(r'^\s*(?:core_)?target\("(\w+)"', re.M)
for xmake in repo_files(lambda name: name.startswith("xmake") and name.endswith(".lua")):
    text = xmake.read_text(encoding="utf-8")
    starts = list(block_start.finditer(text))
    for index, match in enumerate(starts):
        end = starts[index + 1].start() if index + 1 < len(starts) else len(text)
        blocks[match.group(1)].append((xmake.parent, text[match.end():end]))


def listed(xmake_dir, body):
    names = set()
    for entry in re.findall(r'"([^"\n]+)"', body):
        pattern = entry.split("|")[0]
        if not pattern.endswith(".cpp"):
            continue
        if "*" in pattern:
            names |= {match.name for match in xmake_dir.glob(pattern.replace("**.cpp", "**/*.cpp"))}
        else:
            names.add(Path(pattern).name)
    return names


def compiled(name, seen=frozenset()):
    files = set()
    for xmake_dir, body in blocks.get(name, []):
        files |= listed(xmake_dir, body)
        for call in re.findall(r'add_deps\(([^)]*)\)', body):
            for dep in re.findall(r'"(\w+)"', call):
                if dep not in seen and dep != name:
                    files |= compiled(dep, seen | {name})
    return files


compile_only = {name for name, entries in blocks.items()
                if all(re.search(r'set_kind\("(?:static|object)"\)', body) for _, body in entries)}
needed = {name: files for name in blocks if name not in compile_only and (files := compiled(name)) & needs_policy}
missing = sorted(name for name, files in needed.items() if policy not in files)
assert not missing, f"targets compile {header} sources without {policy}: {missing}"
assert {"CommonwealthOnlineUI", "CommonwealthOnlineProbe"} <= set(needed), "contract matched no UI or probe target"

print(f"permanent task policy linkage contract passed: {len(needed)} targets")
