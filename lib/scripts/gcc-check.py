#!/usr/bin/env python3
"""gcc-check.py -- compile every first-party file with GCC's standard library.

    gcc-check.py <build-dir> [--compiler <g++>] [--jobs N]

A stand-in, on a Mac, for the Linux and Windows x64 compiles. Apple's libc++
reaches headers through others that GCC's libstdc++ does not, so a file can be
missing an #include and build on macOS while failing on every other platform --
which is how v0.1.2's first pull request lost four platforms to one
`std::ranges::sort` without <algorithm> (2026-09-25). This compiles each
first-party translation unit (source/ and tests/ of the app whose build
directory is given) with -fsyntax-only, taking the include paths and defines
from that build's own compile_commands.json, so it checks exactly what the
build compiles.

The compiler, unless named: the MinGW-w64 GCC (`x86_64-w64-mingw32-g++`,
`brew install mingw-w64`), preferred because it is the Windows x64 job's own
compiler and takes the code's Windows branches too; else a real GCC (`g++-15`,
..., or a `g++` that is not Apple's clang). Only libcurl's headers are borrowed
from the host -- the ones the build found -- and nothing else of its C library.

Exit status: 0 clean, 1 errors (each file's first lines printed), 3 no GCC
found (pr-ci.sh reports the check as skipped, with the install line).
"""

import argparse
import concurrent.futures
import glob
import json
import os
import re
import shlex
import shutil
import subprocess
import sys
import tempfile

# Options kept from the build's command line, and whether they take a value.
KEEP_WITH_VALUE = {"-I", "-isystem", "-iquote", "-idirafter", "-include", "-D", "-U"}
KEEP_PREFIXES = ("-I", "-isystem", "-iquote", "-D", "-U", "-std=")


def find_compiler():
    for name in ("x86_64-w64-mingw32-g++",):
        path = shutil.which(name)
        if path:
            return path
    versioned = []
    for directory in os.environ.get("PATH", "").split(os.pathsep):
        for path in glob.glob(os.path.join(directory, "g++-[0-9]*")):
            match = re.search(r"g\+\+-(\d+)$", path)
            if match and os.access(path, os.X_OK):
                versioned.append((int(match.group(1)), path))
    if versioned:
        return max(versioned)[1]
    path = shutil.which("g++")
    if path:
        version = subprocess.run([path, "--version"], capture_output=True, text=True).stdout
        if "Free Software Foundation" in version:
            return path
    return None


def command_args(entry):
    if "arguments" in entry:
        return list(entry["arguments"])
    return shlex.split(entry["command"])


def curl_include(entries):
    """The directory holding curl/curl.h that the build itself used."""
    candidates = []
    for entry in entries:
        args = command_args(entry)
        for i, arg in enumerate(args):
            if arg == "-isysroot" and i + 1 < len(args):
                candidates.append(os.path.join(args[i + 1], "usr", "include"))
            elif arg.startswith("-I") or arg == "-isystem":
                value = arg[2:] if arg.startswith("-I") else (args[i + 1] if i + 1 < len(args) else "")
                if value:
                    candidates.append(value)
        break
    try:
        flags = subprocess.run(["pkg-config", "--cflags-only-I", "libcurl"],
                               capture_output=True, text=True).stdout.split()
        candidates += [flag[2:] for flag in flags if flag.startswith("-I")]
    except FileNotFoundError:
        pass
    candidates += ["/usr/include", "/usr/local/include", "/opt/homebrew/include"]
    for candidate in candidates:
        if os.path.isfile(os.path.join(candidate, "curl", "curl.h")):
            return os.path.join(candidate, "curl")
    return None


def gcc_command(compiler, entry, shim):
    args = command_args(entry)
    kept = []
    i = 1
    while i < len(args):
        arg = args[i]
        if arg in KEEP_WITH_VALUE and i + 1 < len(args):
            kept += [arg, args[i + 1]]
            i += 2
            continue
        if arg in ("-o", "-MF", "-MT", "-MQ", "-arch", "-isysroot", "-target", "-x"):
            i += 2
            continue
        if arg.startswith(KEEP_PREFIXES):
            kept.append(arg)
        i += 1
    if not any(arg.startswith("-std=") for arg in kept):
        kept.append("-std=c++20")
    command = [compiler, "-fsyntax-only"] + kept
    if shim:
        command += ["-idirafter", shim]
    return command + [entry["file"]]


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("build_dir")
    parser.add_argument("--compiler")
    parser.add_argument("--jobs", type=int, default=os.cpu_count() or 4)
    options = parser.parse_args()

    database = os.path.join(options.build_dir, "compile_commands.json")
    if not os.path.isfile(database):
        print(f"gcc-check: no {database} -- configure the build first", file=sys.stderr)
        return 1
    app = os.path.realpath(os.path.join(options.build_dir, "..", ".."))
    roots = tuple(os.path.join(app, part) + os.sep for part in ("source", "tests"))
    with open(database, encoding="utf-8") as handle:
        entries = [e for e in json.load(handle)
                   if os.path.realpath(e["file"]).startswith(roots) and e["file"].endswith(".cpp")]
    if not entries:
        print(f"gcc-check: no first-party files in {database}", file=sys.stderr)
        return 1

    compiler = options.compiler or find_compiler()
    if not compiler:
        print("gcc-check: no GCC found -- `brew install mingw-w64` (preferred) or `brew install gcc`",
              file=sys.stderr)
        return 3

    shim_dir = tempfile.mkdtemp(prefix="gcc-check.")
    try:
        curl = curl_include(entries)
        shim = None
        if curl:
            os.symlink(curl, os.path.join(shim_dir, "curl"))
            shim = shim_dir
        print(f"gcc-check: {len(entries)} files with {compiler}"
              f"{'' if curl else ' (libcurl headers not found; files including them will fail)'}")

        def check(entry):
            result = subprocess.run(gcc_command(compiler, entry, shim), capture_output=True,
                                    text=True, cwd=entry.get("directory"))
            return entry["file"], result.returncode, result.stderr

        failed = 0
        with concurrent.futures.ThreadPoolExecutor(max(1, options.jobs)) as pool:
            for path, code, errors in pool.map(check, entries):
                if code != 0:
                    failed += 1
                    lines = [line for line in errors.splitlines() if line.strip()]
                    first = [line for line in lines if "error" in line][:8] or lines[:8]
                    print(f"\n{os.path.relpath(path, app)}:")
                    for line in first:
                        print(f"  {line[:300]}")
    finally:
        shutil.rmtree(shim_dir, ignore_errors=True)

    if failed:
        print(f"\ngcc-check: {failed} of {len(entries)} files do not compile with GCC's library")
        return 1
    print(f"gcc-check: all {len(entries)} files compile with GCC's library")
    return 0


if __name__ == "__main__":
    sys.exit(main())
