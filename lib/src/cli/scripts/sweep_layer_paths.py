#!/usr/bin/env python3
"""Rewrites flat package paths into layered ones across the documentation.

Architecture A2 (2026-10-03) moved every package of the CLI's source into
`source/<layer>/<package>/` and its tests into `tests/<layer>/<package>/`.
This sweep makes the documents say so: `source/agentloop/` becomes
`source/business/agentloop/`, `tests/backends/` becomes `tests/data/backends/`.
Run once over the documents, its diff reviewed as text -- the 2026-08-24
pattern, when the app-rooted rewrite of every seam path was scripted for the
same reason: two dozen documents cannot drift one by one if one script moves
them all.

Short paths -- `agentloop/loop.h`, the spelling an include uses -- are left as
they are: the layer directories are include roots, so a package's short name
is still the name it is included by.

MILESTONES.md is never swept: it records what shipped, and the paths in it
were true when written.

Usage: sweep_layer_paths.py <file>...   (prints each file it changed)
"""
import re
import sys

LAYERS = {
    "presentation": ["commands", "httpserver", "markdown", "render"],
    "business": ["harness", "agentloop", "agent", "tools", "knowledge", "graph", "training",
                 "scaffold", "models", "mcp"],
    "data": ["contracts", "backends", "embedstore", "logger", "secrets", "modelstore",
             "transport"],
    "infrastructure": ["platform", "ansi", "events", "version"],
}
LAYER_OF = {package: layer for layer, packages in LAYERS.items() for package in packages}

# `source/<package>` or `tests/<package>`, at a path boundary: followed by `/`,
# a closing quote or backtick, whitespace, or punctuation -- never a longer
# name (`tests/models_extra` is not `tests/models`).
PATTERN = re.compile(r"(?<![A-Za-z0-9_])(source|tests)/(" + "|".join(LAYER_OF) +
                     r")(?=[/`'\")\s,.;:]|$)")


def sweep(text: str) -> tuple[str, int]:
    return PATTERN.subn(lambda m: f"{m.group(1)}/{LAYER_OF[m.group(2)]}/{m.group(2)}", text)


def main(paths: list[str]) -> int:
    for path in paths:
        if path.endswith("MILESTONES.md"):
            continue
        with open(path, encoding="utf-8") as handle:
            text = handle.read()
        swept, count = sweep(text)
        if count:
            with open(path, "w", encoding="utf-8") as handle:
                handle.write(swept)
            print(f"{count:4d}  {path}")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
