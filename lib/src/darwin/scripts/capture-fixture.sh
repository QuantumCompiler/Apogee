#!/usr/bin/env bash
# capture-fixture.sh -- refresh the recorded protocol fixture from a real
# `apogee` binary, deliberately.
#
#   capture-fixture.sh [apogee-binary]     default: ~/.local/bin/apogee
#
# Writes tests/data/protocol/Fixtures/mock-actor.jsonl: the binary's own stdout
# for the mock-actor conversation -- one turn that asks what to call a note
# (ask_user), writes it behind the permission gate (write_file), and streams
# its answer after a reasoning pass. EventDecoderTests holds the decoder to it,
# so a vocabulary change in a new CLI shows up as a failing test the day this
# is run against that CLI -- never silently.
#
# The binary runs in a throwaway APOGEE_HOME and working folder, on the CLI's
# scripted mock backend: no model, no network, nothing of ~/.apogee touched.
# The driver answers the question `todo.md` and the permission `yes`.
set -euo pipefail

binary="${1:-$HOME/.local/bin/apogee}"
[[ -x "$binary" ]] || { echo "capture-fixture.sh: no executable apogee at ${binary}" >&2; exit 1; }
app_dir="$(cd "$(dirname "$0")/.." && pwd)"
out="${app_dir}/tests/data/protocol/Fixtures/mock-actor.jsonl"
sandbox="$(mktemp -d "${TMPDIR:-/tmp}/apogee-fixture.XXXXXX")"
trap 'rm -rf "$sandbox"' EXIT
mkdir -p "$sandbox/home" "$sandbox/work"
export APOGEE_HOME="$sandbox/home"

cat >"$sandbox/home/actor.json" <<'JSON'
{"turns":[
 {"reasoning":"They want a note written. I should ask what to call it.","text":"","tool_calls":[{"name":"ask_user","arguments":{"questions":[{"header":"Name","question":"What should the note be called?","multi_select":false,"options":[{"label":"todo.md","description":"A to-do list"},{"label":"ideas.md","description":"Loose ideas"}]}]}}]},
 {"text":"","tool_calls":[{"name":"write_file","arguments":{"path":"todo.md","content":"- buy milk\n"}}]},
 {"text":"I wrote todo.md with one item: buy milk.","delay_ms":5}
]}
JSON
"$binary" config init </dev/null >/dev/null
"$binary" config add-backend actor --type mock --model-path "$sandbox/home/actor.json" </dev/null >/dev/null
"$binary" config set-default actor </dev/null >/dev/null

cd "$sandbox/work"
python3 -I - "$binary" >"$out" <<'PY'
import json, subprocess, sys
child = subprocess.Popen(
    [sys.argv[1], "chat", "--output-format", "stream-json", "--input-format", "stream-json", "--tools"],
    stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, bufsize=1)
def send(line):
    child.stdin.write(json.dumps(line) + "\n")
    child.stdin.flush()
send({"type": "hello", "client": {"name": "Apogee", "version": "fixture"}, "wants": ["tools"]})
send({"type": "user", "text": "Write me a note."})
for line in child.stdout:
    sys.stdout.write(line)
    event = json.loads(line)
    if event["type"] == "question":
        send({"type": "answer", "text": "yes" if event.get("kind") == "permission" else "todo.md"})
    if event["type"] in ("result", "error"):
        child.stdin.close()
child.wait(timeout=30)
if child.returncode != 0:
    sys.exit("apogee exited with status %d: %s" % (child.returncode, child.stderr.read().strip()))
PY
echo "capture-fixture.sh: $(wc -l <"$out" | tr -d ' ') lines from $("$binary" version </dev/null | head -n 1) -> ${out}"
