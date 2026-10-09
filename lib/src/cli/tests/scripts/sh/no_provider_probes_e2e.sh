#!/usr/bin/env bash
# Provider probes never run on a startup path (28a), on the real binary.
#
# `cli.no_provider_probes` holds the prober's includes structurally; this is
# the same rule observed from outside: fake `claude`, `codex`, `gemini` and
# `ollama` on PATH, each logging that it ran, a `claude-cli` backend
# configured beside the default mock, and then the paths a person waits on --
# complete, a piped chat, machine mode -- run. Not one fake may have run.
#
# POSIX only.

set -uo pipefail

APOGEE_BIN="${1:?usage: no_provider_probes_e2e.sh <apogee-binary> <work-dir>}"
WORK_DIR="${2:?usage: no_provider_probes_e2e.sh <apogee-binary> <work-dir>}"

rm -rf "$WORK_DIR"
mkdir -p "$WORK_DIR/bin" "$WORK_DIR/home"
export APOGEE_HOME="$WORK_DIR/apogee"
export HOME="$WORK_DIR/home"
trap 'rm -rf "$WORK_DIR"' EXIT

fail() { echo "no_provider_probes: $*" >&2; exit 1; }

LOG="$WORK_DIR/ran.log"
for tool in claude codex gemini ollama; do
    cat >"$WORK_DIR/bin/$tool" <<SCRIPT
#!/bin/sh
echo "$tool \$*" >> "$LOG"
echo "$tool 9.9.9"
SCRIPT
    chmod +x "$WORK_DIR/bin/$tool"
done
export PATH="$WORK_DIR/bin:$PATH"

"$APOGEE_BIN" config init >/dev/null || fail "config init"
"$APOGEE_BIN" config add-backend mock --type mock --model mock-1 >/dev/null || fail "add mock"
"$APOGEE_BIN" config add-backend claude --type claude-cli >/dev/null || fail "add claude-cli"
"$APOGEE_BIN" config set-default mock >/dev/null || fail "set-default"
rm -f "$LOG"

"$APOGEE_BIN" complete "hello" </dev/null >"$WORK_DIR/out.txt" 2>&1 \
    || fail "complete failed: $(cat "$WORK_DIR/out.txt")"
printf 'hello\n' | "$APOGEE_BIN" chat >"$WORK_DIR/chat.txt" 2>&1 \
    || fail "chat failed: $(cat "$WORK_DIR/chat.txt")"
printf '%s\n' '{"type":"user","text":"hi"}' \
    | "$APOGEE_BIN" chat --output-format stream-json --input-format stream-json \
        >"$WORK_DIR/machine.jsonl" 2>"$WORK_DIR/machine-err.txt" \
    || fail "machine mode failed: $(cat "$WORK_DIR/machine-err.txt")"
"$APOGEE_BIN" complete --output-format stream-json "hi" </dev/null \
    >"$WORK_DIR/complete.jsonl" 2>&1 || fail "machine complete failed"

if [ -s "$LOG" ]; then
    fail "a startup path ran a provider binary: $(cat "$LOG")"
fi
[ -e "$APOGEE_HOME/cache/providers.json" ] \
    && fail "a startup path wrote the provider cache (it scanned)"

echo "no_provider_probes: complete, chat and machine mode ran no provider binary - OK"
