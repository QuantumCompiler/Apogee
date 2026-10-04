#!/bin/sh
# The datasets floor end to end, on the real binary: the seeded kits listed,
# a template dataset created, a dataset distilled from the scripted mock as
# the teacher (a named teacher; a vendor CLI refused), the Python boundary --
# `datasets prepare` refusing on a pipe before the environment exists and
# naming `apogee train setup`, `train setup` creating the environment from
# the host's python3 (installing nothing), `check` reporting it, and
# `prepare` converting a JSONL file through the SEEDED script under the
# environment's interpreter -- then list, info, delete.
#
# POSIX only, like the other shell checks. Exits 77 (ctest's skip code)
# when the host has no python3: the environment cannot be created without
# one, and pretending would test nothing.
set -eu

APOGEE_BIN="${1:?usage: datasets_e2e.sh <apogee-binary> <work-dir>}"
WORK_DIR="${2:?usage: datasets_e2e.sh <apogee-binary> <work-dir>}"

rm -rf "$WORK_DIR"
mkdir -p "$WORK_DIR/home"
export APOGEE_HOME="$WORK_DIR/home"
unset ANTHROPIC_API_KEY OPENAI_API_KEY GEMINI_API_KEY GOOGLE_API_KEY
trap 'rm -rf "$WORK_DIR"' EXIT

fail() { echo "datasets_e2e: $*" >&2; exit 1; }

cat > "$WORK_DIR/teacher.json" <<'JSON'
{"turns": [{"text": "[{\"prompt\": \"What is 15% of 240?\", \"completion\": \"36\"}, {\"prompt\": \"If 3x = 21, what is x?\", \"completion\": \"7\"}]"}]}
JSON

"$APOGEE_BIN" config init >/dev/null || fail "config init"
"$APOGEE_BIN" check --fix >/dev/null 2>&1 || fail "check --fix"
"$APOGEE_BIN" config add-backend teacher --type mock --model-path "$WORK_DIR/teacher.json" >/dev/null || fail "add-backend teacher"
"$APOGEE_BIN" config add-backend vendor --type claude-cli >/dev/null || fail "add-backend vendor"
"$APOGEE_BIN" config set-default teacher >/dev/null || fail "set-default"
DATASETS="$APOGEE_HOME/training/datasets"

# --- the seeded kits ---------------------------------------------------------
"$APOGEE_BIN" datasets kits >"$WORK_DIR/kits.txt" || fail "datasets kits"
for kit in instruction-following reasoning structured-output summarization; do
    grep -q "^$kit " "$WORK_DIR/kits.txt" || fail "kit $kit not listed: $(cat "$WORK_DIR/kits.txt")"
done
grep -q "tool-use" "$WORK_DIR/kits.txt" && fail "the deferred tool kit was listed"
[ -f "$APOGEE_HOME/training/scripts/prepare_dataset.py" ] || fail "the script was not seeded"

# --- create and synth --------------------------------------------------------
"$APOGEE_BIN" datasets create starter >/dev/null || fail "datasets create"
[ "$(wc -l < "$DATASETS/starter.jsonl" | tr -d ' ')" = "2" ] || fail "template lines"

"$APOGEE_BIN" datasets synth maths --teacher teacher --kit reasoning --count 2 </dev/null >"$WORK_DIR/synth.txt" 2>&1 || fail "synth: $(cat "$WORK_DIR/synth.txt")"
grep -q "2 example(s)" "$WORK_DIR/synth.txt" || fail "synth count: $(cat "$WORK_DIR/synth.txt")"
grep -q '"role":"user","content":"What is 15% of 240?"' "$DATASETS/maths.jsonl" || fail "synth content: $(cat "$DATASETS/maths.jsonl")"

if "$APOGEE_BIN" datasets synth nope --teacher vendor --kit reasoning </dev/null >/dev/null 2>"$WORK_DIR/vendor.err"; then
    fail "a vendor-CLI teacher was accepted"
fi
grep -q "vendor-CLI" "$WORK_DIR/vendor.err" || fail "vendor refusal wording: $(cat "$WORK_DIR/vendor.err")"
[ ! -f "$DATASETS/nope.jsonl" ] || fail "a refused synth wrote a dataset"

# --- the Python boundary -----------------------------------------------------
printf '{"prompt": "hi", "completion": "hello"}\n' > "$WORK_DIR/raw.jsonl"
if "$APOGEE_BIN" datasets prepare "$WORK_DIR/raw.jsonl" </dev/null >/dev/null 2>"$WORK_DIR/prepare.err"; then
    fail "prepare ran without the environment"
fi
grep -q "apogee train setup" "$WORK_DIR/prepare.err" || fail "prepare refusal wording: $(cat "$WORK_DIR/prepare.err")"
"$APOGEE_BIN" check >"$WORK_DIR/check1.txt" 2>&1 || fail "check before setup: $(cat "$WORK_DIR/check1.txt")"
grep -q "python env" "$WORK_DIR/check1.txt" || fail "check lacks the python env row"

if ! command -v python3 >/dev/null 2>&1; then
    echo "datasets_e2e: no python3 on this host; the environment half is skipped"
    exit 77
fi

"$APOGEE_BIN" train setup >"$WORK_DIR/setup.txt" 2>&1 || fail "train setup: $(cat "$WORK_DIR/setup.txt")"
[ -x "$APOGEE_HOME/training/venv/bin/python" ] || fail "no interpreter in the environment"
[ -f "$APOGEE_HOME/training/venv/apogee.json" ] || fail "no environment record"
"$APOGEE_BIN" check >"$WORK_DIR/check2.txt" 2>&1 || fail "check after setup"
grep -q "ok   python env" "$WORK_DIR/check2.txt" || fail "check does not report the environment: $(grep 'python env' "$WORK_DIR/check2.txt")"

"$APOGEE_BIN" datasets prepare "$WORK_DIR/raw.jsonl" </dev/null >"$WORK_DIR/prepare.txt" 2>&1 || fail "prepare: $(cat "$WORK_DIR/prepare.txt")"
grep -q "1 row(s) written" "$WORK_DIR/prepare.txt" || fail "prepare summary: $(cat "$WORK_DIR/prepare.txt")"
grep -q '"role": "assistant", "content": "hello"' "$DATASETS/raw.jsonl" || fail "prepared content: $(cat "$DATASETS/raw.jsonl")"

# --- list, info, delete ------------------------------------------------------
"$APOGEE_BIN" datasets list >"$WORK_DIR/list.txt" || fail "datasets list"
grep -q "^maths " "$WORK_DIR/list.txt" || fail "maths not listed"
grep -q "^raw " "$WORK_DIR/list.txt" || fail "raw not listed"
"$APOGEE_BIN" datasets info maths | grep -q "lines:  2" || fail "datasets info"
"$APOGEE_BIN" datasets delete maths -y >/dev/null || fail "datasets delete"
[ ! -f "$DATASETS/maths.jsonl" ] || fail "delete left the file"

echo "apogee datasets end-to-end: OK"
