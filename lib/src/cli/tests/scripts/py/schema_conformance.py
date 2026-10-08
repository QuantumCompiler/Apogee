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

    # The declarations (28d): the vocabulary `capabilities` announces is a
    # table in the source, and it must be exactly what the emitter and the
    # parser speak -- a type announced but never written, or written but not
    # announced, misleads a host that trusts the session event.
    def declared(name):
        match = re.search(name + r"\{([^}]*)\}", code)
        if match is None:
            failures.append(f"{source} declares no {name} -- the capabilities table moved; "
                            f"this check is now blind to it")
            return set()
        return set(re.findall(r'"([a-z_]+)"', match.group(1)))

    declared_out = declared("kEventTypes")
    declared_in = declared("kInboundTypes")
    for kind in sorted(emitted - declared_out):
        failures.append(f"the emitter produces '{kind}' but kEventTypes never announces it")
    for kind in sorted(declared_out - emitted):
        failures.append(f"kEventTypes announces '{kind}' but nothing emits it")
    for kind in sorted(accepted ^ declared_in):
        failures.append(f"'{kind}' is in one of kInboundTypes and the parser but not the other")

    # The session event's fields, and the capabilities object's (28d): each
    # named in the document, so a field a host reads is never undocumented.
    body = re.search(r"void JsonReporter::begin_session\(.*?\n\}\n", code, re.DOTALL)
    if body is None:
        failures.append(f"{source} has no begin_session -- the session event moved")
    else:
        for field in sorted(set(re.findall(r'\["([a-z_]+)"\]', body.group(0)))):
            if f'"{field}"' not in prose and f"`{field}`" not in prose and \
                    f"`capabilities.{field}`" not in prose:
                failures.append(f"the session event carries '{field}' but {document} never "
                                f"names it")

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
