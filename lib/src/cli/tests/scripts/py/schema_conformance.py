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

Since 28d it also holds the declaration `capabilities` announces, and since
28g that declaration is `machine/protocol.cpp` -- read from beside
`json_reporter.cpp` -- and, given the binary, the schema it prints
(`apogee __machine-schema`): the third view of the one vocabulary, held to the
code and the document, its tolerance and its strictness checked with a stock
validator when one is installed (said when not).

Usage: schema_conformance.py <json_reporter.cpp> <machine-mode.md> [<apogee>]
"""

import json
import os
import re
import subprocess
import sys


def main():
    if len(sys.argv) not in (3, 4):
        print(__doc__)
        return 2
    source, document = sys.argv[1], sys.argv[2]
    binary = sys.argv[3] if len(sys.argv) == 4 else None
    declaration = os.path.join(os.path.dirname(source), "protocol.cpp")
    with open(declaration, encoding="utf-8") as handle:
        protocol = handle.read()

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
        match = re.search(name + r"\{\{(.*?)\n\}\};", protocol, re.DOTALL)
        if match is None:
            failures.append(f"{declaration} declares no {name} -- the declaration moved; "
                            f"this check is now blind to it")
            return set()
        return set(re.findall(r'LineSpec\{"([a-z_]+)"', match.group(1)))

    declared_out = declared("kOutbound")
    declared_in = declared("kInbound")
    for kind in sorted(emitted - declared_out):
        failures.append(f"the emitter produces '{kind}' but kOutbound never declares it")
    for kind in sorted(declared_out - emitted):
        failures.append(f"kOutbound declares '{kind}' but nothing emits it")
    for kind in sorted(accepted ^ declared_in):
        failures.append(f"'{kind}' is in one of kInbound and the parser but not the other")

    # The schema the binary prints (28g): exactly the declaration, a valid
    # 2020-12 document, tolerant of what a later build adds and strict about
    # what this one promises.
    if binary is not None:
        printed = subprocess.run([binary, "__machine-schema"], capture_output=True, text=True,
                                 stdin=subprocess.DEVNULL, timeout=60)
        if printed.returncode != 0:
            failures.append(f"__machine-schema failed: {printed.stderr.strip()}")
        else:
            schema = json.loads(printed.stdout)
            defs = schema.get("$defs", {})
            schema_out = {k[len("event_"):] for k in defs if k.startswith("event_")}
            schema_in = {k[len("line_"):] for k in defs if k.startswith("line_")}
            for kind in sorted(schema_out ^ emitted):
                failures.append(f"the schema and the emitter disagree on '{kind}'")
            for kind in sorted(schema_in ^ accepted):
                failures.append(f"the schema and the parser disagree on inbound '{kind}'")
            for kind in sorted((schema_out | schema_in) - documented):
                failures.append(f"the schema names '{kind}' but {document} never describes it")
            try:
                from jsonschema import Draft202012Validator
            except ImportError:
                print("schema conformance: no jsonschema module -- the schema's validity and "
                      "tolerance were NOT checked (pip install jsonschema to check them)",
                      file=sys.stderr)
            else:
                Draft202012Validator.check_schema(schema)
                valid = Draft202012Validator(schema)
                def ok(event):
                    return not list(valid.iter_errors(event))
                if not ok({"type": "a_type_from_a_later_build", "anything": 1}):
                    failures.append("the schema rejects an unknown event type")
                if not ok({"type": "error", "message": "x", "field_from_later": True}):
                    failures.append("the schema rejects an unknown field on a known event")
                if ok({"type": "result", "model": "m", "finish_reason": "stop"}):
                    failures.append("the schema accepts a result with no text")
                if ok({"type": "answer_delta", "text": 7}):
                    failures.append("the schema accepts an answer_delta whose text is a number")

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

    # The field every event of a turn carries (28f), stamped in one place.
    writer = re.search(r"void JsonReporter::write\(.*?\n\}\n", code, re.DOTALL)
    if writer is None:
        failures.append(f"{source} has no JsonReporter::write -- the writer moved")
    else:
        for field in sorted(set(re.findall(r'\["([a-z_]+)"\]', writer.group(0)))):
            if f'"{field}"' not in prose and f"`{field}`" not in prose:
                failures.append(f"every event of a turn carries '{field}' but {document} never "
                                f"names it")
    # A cancelled turn's result says so (28f): the value a host switches on.
    if '"cancelled"' not in prose:
        failures.append(f'{document} never names finish_reason "cancelled", which a cancelled '
                        f"turn's result carries")

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
