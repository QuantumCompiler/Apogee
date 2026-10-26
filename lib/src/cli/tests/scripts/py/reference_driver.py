#!/usr/bin/env python3
"""The reference driver for Apogee's machine mode -- and its test-lock.

This file is deliberately two things at once. It is the **worked example** a
GUI author reads to see how little it takes to drive Apogee: spawn it once,
write one JSON object per line to its stdin, read one JSON object per line back.
And it is the **test** that the example stays true, by reconstructing the
conversation from the event stream and checking that reconstruction against
what the terminal renderer printed for the same prompt.

That check is the point. Machine mode earns its keep only if a driver sees
exactly what a terminal user sees. Three independent paths have to agree:

  1. the answer_delta chunks, concatenated
  2. the terminal result event's `text`
  3. what `apogee complete` printed in text mode

If they ever disagree, one surface has grown a behaviour the other lacks, which
is the failure the shared Reporter seam exists to prevent.

Two disciplines a real driver needs, both exercised here:

  * **Ignore unknown event types.** New types are added without a version bump,
    so a driver that treats an unfamiliar type as an error breaks on upgrade.
  * **Drop `thinking*` events.** Reasoning is distinctly typed precisely so a
    driver can discard it. Dropping every thinking event must leave exactly the
    answer.

And the launch, both ways (M29): a session under a suite names it on its
`session` event and says what it takes of the machine as a `notice` after it;
a launch the child refuses writes no `session` at all -- stdout closes empty,
the child exits 1, and stderr says why.

Usage: reference_driver.py <apogee-binary> <work-dir>
"""

import json
import os
import shutil
import subprocess
import tempfile
import sys


def drive(apogee, env, prompts, flags=()):
    """Run ONE apogee child through several turns, the way a GUI would.

    The child stays alive across turns -- that is the whole reason machine mode
    is a persistent stdio process rather than a subprocess per question. It
    keeps conversation history in one place, and it is why the transcript
    written at exit contains the whole conversation.
    """
    child = subprocess.Popen(
        # Both directions, explicitly. `--input-format` alone would be
        # refused: a driven session speaks the protocol both ways.
        [apogee, "chat",
         "--output-format", "stream-json",
         "--input-format", "stream-json", *flags],
        stdin=subprocess.PIPE,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        env=env,
        text=True,
        bufsize=1,
    )

    for prompt in prompts:
        child.stdin.write(json.dumps({"type": "user", "text": prompt}) + "\n")
    child.stdin.close()

    turns = []           # one reconstructed answer per turn
    results = []         # the text each result event reported
    session = None
    answer = []
    dropped = []
    notices = []

    for line in child.stdout:
        line = line.strip()
        if not line:
            continue

        event = json.loads(line)          # every line is a JSON object
        kind = event.get("type")

        if kind == "session":
            session = event
        elif kind == "answer_start":
            answer = []
        elif kind == "answer_delta":
            answer.append(event["text"])
        elif kind == "answer_end":
            turns.append("".join(answer))
        elif kind == "result":
            results.append(event["text"])
        elif kind == "error":
            print(f"driver: apogee reported: {event['message']}", file=sys.stderr)
        elif kind == "notice":
            # Words for the user that are neither progress nor an error: shown
            # and kept, never fatal.
            notices.append(event["text"])
        else:
            # thinking, thinking_delta, tool_status, and anything a later build
            # introduces. Dropping them silently is REQUIRED of a driver, not a
            # shortcut taken here.
            dropped.append(kind)

    code = child.wait(timeout=60)
    return {"session": session, "turns": turns, "results": results, "dropped": dropped,
            "notices": notices, "stderr": child.stderr.read(), "code": code}


def main():
    if len(sys.argv) != 3:
        print(__doc__)
        return 2
    apogee, work = sys.argv[1], sys.argv[2]

    # A home of this run's own beside the path ctest gives, removed when the
    # run ends: two runs of the suite in one build tree once shared it (M17).
    os.makedirs(os.path.dirname(work), exist_ok=True)
    work = tempfile.mkdtemp(prefix=os.path.basename(work) + ".", dir=os.path.dirname(work))
    try:
        return run(apogee, work)
    finally:
        shutil.rmtree(work, ignore_errors=True)


def run(apogee, work):
    env = dict(os.environ, APOGEE_HOME=work)

    for args in (["config", "init"],
                 ["config", "add-backend", "mock", "--type", "mock", "--model", "mock-1"],
                 ["config", "set-default", "mock"]):
        subprocess.run([apogee, *args], env=env, check=True,
                       stdout=subprocess.DEVNULL)

    prompts = ["what is 2+2?", "and 3+3?"]
    first = drive(apogee, env, prompts)
    session, turns, results, dropped, err = (first["session"], first["turns"], first["results"],
                                             first["dropped"], first["stderr"])

    failures = []

    if session is None:
        failures.append("no session event: a driver cannot know what it is talking to")
    else:
        if "protocol_version" not in session:
            failures.append("the session event carries no protocol_version")
        if not session.get("model"):
            failures.append("the session event names no model")

    if len(turns) != len(prompts):
        failures.append(f"expected {len(prompts)} reconstructed answers, got {len(turns)}")
    if len(results) != len(prompts):
        failures.append(f"expected {len(prompts)} result events, got {len(results)}")

    # (1) == (2): the deltas rebuild exactly what the result reports. A driver
    # rendering live and a driver reading only the result must not diverge.
    for index, (streamed, reported) in enumerate(zip(turns, results)):
        if streamed != reported:
            failures.append(
                f"turn {index}: streamed {streamed!r} but the result said {reported!r}")

    # (2) == (3): and both match what a terminal user was shown.
    rendered = subprocess.run(
        [apogee, "complete", prompts[0]], env=env, check=True,
        stdout=subprocess.PIPE, text=True).stdout.strip()
    if turns and turns[0] != rendered:
        failures.append(
            f"the driver reconstructed {turns[0]!r} but the terminal rendered {rendered!r}")

    # Dropping every thinking event must have cost the driver nothing.
    for turn in turns:
        if "thinking" in turn.lower() and "thinking" not in prompts[0].lower():
            failures.append(f"reasoning leaked into an answer: {turn!r}")

    if err.strip() and "error" in err.lower():
        failures.append(f"stderr reported an error: {err.strip()}")

    # Under a suite, the session names it, and what it takes of the machine is
    # a notice -- read from the stream, never from stderr (M29).
    subprocess.run([apogee, "config", "add-suite", "pair", "--chat", "mock"], env=env,
                   check=True, stdout=subprocess.DEVNULL)
    suited = drive(apogee, env, ["hello"], ("--suite", "pair"))
    if suited["session"] is None or suited["session"].get("suite") != "pair":
        failures.append(f"a session under a suite does not name it: {suited['session']}")
    if not any(note.startswith("suite pair") for note in suited["notices"]):
        failures.append(f"the suite's admission is no notice: {suited['notices']}")
    if "suite pair" in suited["stderr"]:
        failures.append(f"the suite's admission went to stderr: {suited['stderr'].strip()}")

    # A launch refused: no session event -- nothing at all on stdout -- exit
    # 1, and the reason on stderr, where a driver reads why it never began.
    refused = drive(apogee, env, ["hello"], ("--suite", "nope"))
    if refused["session"] is not None or refused["turns"] or refused["notices"]:
        failures.append("a refused launch wrote on stdout")
    if refused["code"] != 1:
        failures.append(f"a refused launch exited {refused['code']}, not 1")
    if "no suite named 'nope'" not in refused["stderr"]:
        failures.append(f"a refused launch did not say why on stderr: {refused['stderr'].strip()}")

    if failures:
        print("reference driver: the machine-mode contract is broken", file=sys.stderr)
        for failure in failures:
            print(f"  - {failure}", file=sys.stderr)
        return 1

    print(f"reference driver: {len(turns)} turns reconstructed from JSONL, "
          f"identical to the terminal rendering "
          f"(dropped {len(dropped)} non-answer events) - OK")
    return 0


if __name__ == "__main__":
    sys.exit(main())
