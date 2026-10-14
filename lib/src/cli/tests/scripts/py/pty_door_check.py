#!/usr/bin/env python3
"""Every classified door, reached on a fresh install (44d, ADR 0012).

The parity law (ADR 0010) holds the shell's table to the registered commands,
which proves a classification and not a door: for a day `execute` was
classified a view while a fresh install had no picker to show it. This walks
the doors themselves, on a real terminal:

  table   The `View` rows of `cli/tui_parity.cpp`'s table, read from the source
          the binary was compiled from -- every row a `ShellSurface{...}` with
          its `ShellDoor{.view = ...}` spelled out. A view row this cannot read
          a door for fails, named; so does a table with no view, or one without
          the execute door the law exists for.
  fresh   A data directory as `apogee check --fix` seeds it, and nothing else:
          no backend, no suite, no saved chat -- what a new user has.
  walk    One `apogee` on a pseudo-terminal, every door in the table's order:
          its view reached through the Go-to menu (Ctrl-G, its title's first
          letter until the tab strip's bracket names it, Enter), and what the
          row says the view shows for it seen there -- for a conditional door,
          the words that say its condition. Then Ctrl-D, the terminal's modes
          as they were.

POSIX only, like every PTY check.
"""

import os
import re
import subprocess
import sys
import tempfile
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from pty_tui_check import ENTER_ALT, LEAVE_ALT, Session, fail, modes_and_exit  # noqa: E402

ROW = re.compile(r'ShellSurface\{\s*"([^"]+)"\s*,\s*(\w+)\s*,')
FIELD = re.compile(r'\.(view|seen|condition)\s*=\s*"([^"]*)"')
BRACKET = re.compile(rb"\[\d+ ([^\]\r\n]+)\]")


def view_rows(source):
    """Each `View` row's command and door, in the table's order."""
    with open(source, encoding="utf-8") as text:
        code = text.read()
    start = code.find("kSurfaces{")
    end = code.find("};", start)
    if start < 0 or end < 0:
        fail("table", f"no kSurfaces table in {source}")
    table = code[start:end]
    found = list(ROW.finditer(table))
    rows = []
    for index, match in enumerate(found):
        command, kind = match.group(1), match.group(2)
        if kind != "View":
            continue
        body = table[match.end():found[index + 1].start() if index + 1 < len(found) else len(table)]
        door = re.search(r"ShellDoor\{([^}]*)\}", body)
        fields = dict(FIELD.findall(door.group(1))) if door else {}
        if not fields.get("view"):
            fail("table", f"'{command}' is a view whose door this cannot read -- spell out its "
                          "ShellDoor{.view = ...}")
        rows.append((command, fields["view"], fields.get("seen", ""), fields.get("condition", "")))
    if not rows:
        fail("table", "no view rows read -- the walk would pass vacuously")
    if not any(command == "execute" for command, _, _, _ in rows):
        fail("table", "the execute door is not among the views -- the door the law exists for")
    return rows


def current_view(output):
    """The view the tab strip's last bracket names, or None."""
    found = BRACKET.findall(output)
    return found[-1].decode("utf-8", "replace") if found else None


def reach(session, command, title, seen):
    """Walks to `title` through the Go-to menu and sees `seen` there."""
    for _ in range(40):
        session.send(b"\x07")  # Ctrl-G
        session.wait_for(b"Go to", 5, command)
        session.send(title[0].lower().encode())
        session.read_for(0.15)
        mark = len(session.output)
        session.send(b"\r")
        session.read_for(0.4)
        if current_view(session.output[mark:]) == title:
            break
    else:
        fail(command, f"the {title} view was never reached through the Go-to menu",
             session.output)
    if seen:
        deadline = time.time() + 5
        while seen.encode() not in session.output[mark:]:
            if time.time() > deadline or session.closed:
                fail(command, f"the {title} view never showed '{seen}' on a fresh install -- "
                              "the door this row classifies is not there", session.output)
            session.read_for(0.1)


def main():
    binary, source = sys.argv[1], sys.argv[2]
    rows = view_rows(source)
    with tempfile.TemporaryDirectory(prefix="apogee-doors-") as home:
        env = dict(os.environ, TERM="xterm-256color", APOGEE_HOME=home, HOME=home)
        env.pop("NO_COLOR", None)
        seeded = subprocess.run([binary, "check", "--fix"], stdin=subprocess.DEVNULL,
                                capture_output=True, env=env)
        if seeded.returncode != 0:
            fail("fresh", f"check --fix exited {seeded.returncode}", seeded.stdout + seeded.stderr)

        session = Session(f'cd "{home}" && stty -g; "{binary}"; echo "EXIT:$?"; stty -g', env)
        session.wait_for(b"[1 Session]", 15, "walk")
        session.wait_for(b"New chat", 15, "walk")
        for command, title, seen, condition in rows:
            reach(session, command, title, seen)
            said = f" (conditional on {condition})" if condition else ""
            print(f"ok   {command}: {title}{' -- ' + repr(seen) if seen else ''}{said}")
        session.send(b"\x07")
        session.wait_for(b"Go to", 5, "walk")
        session.send(b"\x04")  # Ctrl-D, from the menu: the shell quits
        output = session.finish("walk")
        before, after, code = modes_and_exit("walk", output)
        if code != 0:
            fail("walk", f"exit {code}, expected 0", output)
        if ENTER_ALT not in output or output.rfind(LEAVE_ALT) < output.rfind(ENTER_ALT):
            fail("walk", "the alternate screen was not entered and left", output)
        if before != after:
            fail("walk", f"the terminal's modes changed:\n  before {before}\n  after  {after}",
                 output)
        print(f"ok   walk: {len(rows)} doors reached on a fresh install, modes restored")


if __name__ == "__main__":
    main()
