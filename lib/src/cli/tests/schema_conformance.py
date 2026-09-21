#!/usr/bin/env python3
"""Pins the documented protocol against the one the code actually speaks.

A protocol document that drifts from its implementation is worse than none: a
GUI author trusts it, builds against it, and debugs Apogee for a fault that is
in the prose. So the document is not decorative here -- every event type the
emitter can produce must appear in it, and every type it describes must exist.

Deliberately structural, not semantic. It checks the *vocabulary* in both
directions, which is the part that silently rots when someone adds an event and
forgets the doc. Field-level meaning is pinned by machine_mode_test.cpp, where
the values are actually available to assert on.

Usage: schema_conformance.py <json_reporter.cpp> <machine-mode.md>
"""

import re
import sys


def main():
    if len(sys.argv) != 3:
        print(__doc__)
        return 2
    source, document = sys.argv[1], sys.argv[2]

    with open(source, encoding="utf-8") as handle:
        code = handle.read()
    with open(document, encoding="utf-8") as handle:
        prose = handle.read()

    # What the emitter can produce, and what it accepts from a driver.
    emitted = set(re.findall(r'event\("([a-z_]+)"\)', code))
    accepted = set(re.findall(r'type == "([a-z_]+)"', code))

    if not emitted:
        print("schema_conformance: found no event types in the source -- has the "
              "emitter changed shape? This check is now blind.", file=sys.stderr)
        return 1

    # What the document describes: the JSONL fences plus the reference table.
    documented = set(re.findall(r'\{"type":"([a-z_]+)"', prose))
    documented |= set(re.findall(r'^\| `([a-z_]+)`', prose, re.MULTILINE))

    failures = []

    for kind in sorted(emitted - documented):
        failures.append(
            f"the emitter produces '{kind}' but {document} never describes it -- "
            f"a driver has no way to know it exists")

    for kind in sorted(accepted - documented):
        failures.append(
            f"the child accepts '{kind}' on stdin but {document} never documents "
            f"how to send it")

    # The other direction: prose promising something the code cannot deliver.
    known = emitted | accepted
    for kind in sorted(documented - known):
        failures.append(
            f"{document} describes '{kind}' but nothing emits or accepts it -- "
            f"a driver waiting for it would wait forever")

    if failures:
        print("schema conformance: the protocol document and the code disagree",
              file=sys.stderr)
        for failure in failures:
            print(f"  - {failure}", file=sys.stderr)
        return 1

    print(f"schema conformance: {len(emitted)} emitted and {len(accepted)} accepted "
          f"event types, all documented - OK")
    return 0


if __name__ == "__main__":
    sys.exit(main())
