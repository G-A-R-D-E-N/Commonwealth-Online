import argparse
import json
from pathlib import Path
import sys

CLASS_KEYS = ("story", "companion", "vendor", "town", "settler", "enemy", "animal")
DATA_FILE_NAME = "world-policy.json"
SAMPLE_FILE_NAME = "world-policy.sample.json"
CONFIG_FILE_NAME = "commonwealth-server-native.json"
CONFIG_KEYS = ("worldMode", "doorsShared", "worldPolicyPath")


def check_data_file(server_dir):
    problems = []
    path = server_dir / DATA_FILE_NAME
    if not path.is_file():
        problems.append(
            f"{path}: missing; generate the real world policy file and copy it here"
        )
        return problems
    try:
        root = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, ValueError) as error:
        problems.append(f"{path}: not readable JSON ({error})")
        return problems
    if not isinstance(root, dict):
        problems.append(f"{path}: root must be an object")
        return problems
    if root.get("sample") is True:
        problems.append(f"{path}: carries the sample marker; it is the placeholder, not generated data")
    version = root.get("version")
    if not isinstance(version, int) or isinstance(version, bool) or version < 1:
        problems.append(f"{path}: version must be an integer >= 1")
    classes = root.get("classes")
    if not isinstance(classes, dict):
        problems.append(f"{path}: classes must be an object")
        return problems
    for key in CLASS_KEYS:
        if key not in classes:
            problems.append(f"{path}: classes.{key} is missing")
    if problems:
        return problems
    story = classes.get("story") or []
    companion = classes.get("companion") or []
    if not story and not companion:
        problems.append(
            f"{path}: story and companion are both empty, so free-roam would run with an empty disable list"
        )
    return problems


def check_config_template(repo_root):
    problems = []
    path = repo_root / "CO-SERVER/server-cpp" / CONFIG_FILE_NAME
    if not path.is_file():
        problems.append(f"{path}: missing")
        return problems
    try:
        root = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, ValueError) as error:
        problems.append(f"{path}: not readable JSON ({error})")
        return problems
    if not isinstance(root, dict):
        problems.append(f"{path}: root must be an object")
        return problems
    for key in CONFIG_KEYS:
        if key not in root:
            problems.append(f"{path}: {key} is missing; the host UI writes the camelCase spelling")
    return problems


def main():
    parser = argparse.ArgumentParser(description="Fail when a release server folder cannot run the shipped world modes.")
    parser.add_argument("server_dirs", nargs="+", type=Path)
    parser.add_argument("--repo-root", type=Path, default=Path(__file__).resolve().parents[1])
    args = parser.parse_args()

    problems = check_config_template(args.repo_root)
    for server_dir in args.server_dirs:
        if not server_dir.is_dir():
            problems.append(f"{server_dir}: not a directory")
            continue
        if (server_dir / SAMPLE_FILE_NAME).is_file() and not (server_dir / DATA_FILE_NAME).is_file():
            problems.append(f"{server_dir}: ships only {SAMPLE_FILE_NAME}")
        problems.extend(check_data_file(server_dir))

    for problem in problems:
        print(problem, file=sys.stderr)
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main())
