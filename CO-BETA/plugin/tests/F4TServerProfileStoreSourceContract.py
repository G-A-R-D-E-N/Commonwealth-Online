from pathlib import Path


root = Path(__file__).resolve().parents[3]
source = (root / "CO-BETA/plugin/src/F4TServerProfileStore.cpp").read_text(encoding="utf-8")
start = source.index("bool CommitFileToDisk")
end = source.index("\n\tvoid EnsureLoaded", start)
commit = source[start:end]

assert "_O_WRONLY" in commit
assert "_O_BINARY" in commit
assert "_O_RDONLY" not in commit
