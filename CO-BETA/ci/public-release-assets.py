#!/usr/bin/env python3
import json
import os
import sys


def urls(node):
    if isinstance(node, dict):
        for key, value in node.items():
            if key == "url":
                yield value
            else:
                yield from urls(value)
    elif isinstance(node, list):
        for value in node:
            yield from urls(value)


def main(directory):
    with open(os.path.join(directory, "latest.json"), encoding="utf-8") as handle:
        names = sorted({os.path.basename(url) for url in urls(json.load(handle))})
    for name in names + ["latest.json", "latest.json.sig"]:
        print(os.path.join(directory, name))


if __name__ == "__main__":
    main(sys.argv[1])
