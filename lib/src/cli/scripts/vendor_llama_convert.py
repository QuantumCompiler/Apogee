#!/usr/bin/env python3
"""Re-vendor llama.cpp's HF -> GGUF converter at a new pin.

    python3 scripts/vendor_llama_convert.py --from <llama.cpp tree>   # run from lib/src/cli

<llama.cpp tree> is a checkout (or an extracted archive) of the revision the
pin in third_party/CMakeLists.txt now names -- after a configure, the
FetchContent copy under build/<preset>/_deps/llama_cpp-src is exactly that.

In order:

  1. Every file vendored now is recorded in
     third_party/llama.cpp-convert/retired-digests.txt as `<name> <sha256>`,
     BEFORE anything is replaced: a user's ~/.apogee still holds these
     bytes, and the seeding path can tell "an earlier Apogee's copy" (update
     it) from "the user's edit" (keep it) only by this list.
  2. The converter is replaced: the entry script, conversion/, the `gguf`
     package (gguf-py/gguf/*.py, not its scripts/), both licenses, and every
     template under models/templates/ that conversion/ reads by name.
  3. scripts/generate_training_assets.py compiles the result in.

Nothing is edited on the way in (third_party/README.md). A pin bump is its
own commit; the message says what changed upstream and why Apogee wants it.
"""

from __future__ import annotations

import argparse
import hashlib
import re
import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
VENDORED = ROOT / "third_party" / "llama.cpp-convert"
RETIRED = VENDORED / "retired-digests.txt"
KEEP = {"README.md", RETIRED.name}


def shipped_files(tree: Path) -> list[Path]:
    """What the generator compiles in: every .py and .jinja file."""
    return sorted(p for p in tree.rglob("*") if p.is_file() and p.suffix in {".py", ".jinja"})


def record_retired() -> int:
    lines = set()
    if RETIRED.exists():
        lines = {line.strip() for line in RETIRED.read_text(encoding="utf-8").splitlines()
                 if line.strip() and not line.startswith("#")}
    before = len(lines)
    for path in shipped_files(VENDORED):
        name = "convert/" + path.relative_to(VENDORED).as_posix()
        lines.add(f"{name} {hashlib.sha256(path.read_bytes()).hexdigest()}")
    header = (
        "# Every converter file version an earlier Apogee shipped, as `<name> <sha256>`.\n"
        "# Written by scripts/vendor_llama_convert.py before each re-vendor; never edited.\n"
        "# A seeded file matching a line is that Apogee's copy, and is updated in place.\n"
    )
    RETIRED.write_text(header + "\n".join(sorted(lines)) + "\n", encoding="utf-8")
    return len(lines) - before


def templates_read_by_name(conversion: Path, templates: Path) -> list[Path]:
    """The templates conversion/ opens by path -- the rest of upstream's
    models/templates/ is never consulted by the converter."""
    names = set()
    for module in conversion.glob("*.py"):
        names.update(re.findall(r"[\"']([A-Za-z0-9_.\-]+\.jinja)[\"']", module.read_text(encoding="utf-8")))
    return sorted(templates / name for name in names if (templates / name).is_file())


def replace(source: Path) -> None:
    for entry in VENDORED.iterdir():
        if entry.name in KEEP:
            continue
        if entry.is_dir():
            shutil.rmtree(entry)
        else:
            entry.unlink()

    shutil.copy2(source / "convert_hf_to_gguf.py", VENDORED / "convert_hf_to_gguf.py")
    shutil.copy2(source / "LICENSE", VENDORED / "LICENSE")
    (VENDORED / "conversion").mkdir()
    for module in sorted((source / "conversion").glob("*.py")):
        shutil.copy2(module, VENDORED / "conversion" / module.name)
    (VENDORED / "gguf-py" / "gguf").mkdir(parents=True)
    shutil.copy2(source / "gguf-py" / "LICENSE", VENDORED / "gguf-py" / "LICENSE")
    for module in sorted((source / "gguf-py" / "gguf").glob("*.py")):
        shutil.copy2(module, VENDORED / "gguf-py" / "gguf" / module.name)
    (VENDORED / "models" / "templates").mkdir(parents=True)
    for template in templates_read_by_name(source / "conversion", source / "models" / "templates"):
        shutil.copy2(template, VENDORED / "models" / "templates" / template.name)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--from", dest="source", required=True, type=Path,
                        help="a llama.cpp tree at the new pin")
    args = parser.parse_args()
    source: Path = args.source.resolve()
    for required in ("convert_hf_to_gguf.py", "conversion", "gguf-py/gguf", "models/templates"):
        if not (source / required).exists():
            raise SystemExit(f"{source} has no {required} -- is it a llama.cpp tree?")

    added = record_retired()
    replace(source)
    print(f"recorded {added} retired digest(s); vendored {len(shipped_files(VENDORED))} files")
    subprocess.run([sys.executable, str(ROOT / "scripts" / "generate_training_assets.py")], check=True)
    return 0


if __name__ == "__main__":
    sys.exit(main())
