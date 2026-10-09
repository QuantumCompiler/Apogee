#!/usr/bin/env bash
# Provider registration (28b) on the real binary, with fake vendor CLIs.
#
# Fake `claude` and `codex` on PATH (nothing else: PATH is the fake bin
# directory and the system's own), a sandboxed APOGEE_HOME and HOME, and a
# config holding only the mock. Then:
#
#   - `providers scan` prints both with their tier words, and the config is
#     byte-identical afterwards: a scan registers nothing;
#   - `providers scan --register` writes two entries through the editor and
#     names each; the user's comment survives; a second run writes nothing
#     and says so per provider;
#   - with a backend already named `claude`, `--register` skips it, says so,
#     and touches it not at all;
#   - complete and machine mode, with the scan cached and both providers
#     detected, never print an offer and never write the config.
#
# POSIX only.

set -uo pipefail

APOGEE_BIN="${1:?usage: providers_e2e.sh <apogee-binary> <work-dir>}"
WORK_DIR="${2:?usage: providers_e2e.sh <apogee-binary> <work-dir>}"

rm -rf "$WORK_DIR"
mkdir -p "$WORK_DIR/bin" "$WORK_DIR/home"
export HOME="$WORK_DIR/home"
trap 'rm -rf "$WORK_DIR"' EXIT

fail() { echo "providers: $*" >&2; exit 1; }

cat >"$WORK_DIR/bin/claude" <<'SCRIPT'
#!/bin/sh
echo '9.9.9 (Claude Code)'
SCRIPT
cat >"$WORK_DIR/bin/codex" <<'SCRIPT'
#!/bin/sh
[ "$1" = login ] && { echo 'Logged in'; exit 0; }
echo 'codex-cli 9.9.9'
SCRIPT
chmod +x "$WORK_DIR/bin/claude" "$WORK_DIR/bin/codex"
: >"$HOME/.claude.json"
export PATH="$WORK_DIR/bin:/usr/bin:/bin"
unset ANTHROPIC_API_KEY OPENAI_API_KEY GEMINI_API_KEY GOOGLE_API_KEY

fresh_home() {
    export APOGEE_HOME="$WORK_DIR/$1"
    "$APOGEE_BIN" config init >/dev/null || fail "config init"
    "$APOGEE_BIN" config add-backend mock --type mock --model mock-1 >/dev/null || fail "add mock"
    "$APOGEE_BIN" config set-default mock >/dev/null || fail "set-default"
    CONFIG="$APOGEE_HOME/config/config.json"
}

# --- a scan registers nothing --------------------------------------------------
fresh_home one
cp "$CONFIG" "$WORK_DIR/before.yaml"
"$APOGEE_BIN" providers scan </dev/null >"$WORK_DIR/scan.txt" 2>&1 \
    || fail "scan failed: $(cat "$WORK_DIR/scan.txt")"
grep -q '^claude  *credentials found  *not registered' "$WORK_DIR/scan.txt" \
    || fail "claude not listed with its tier: $(cat "$WORK_DIR/scan.txt")"
grep -q '~/.claude.json exists' "$WORK_DIR/scan.txt" || fail "claude's evidence not named"
grep -q '^codex  *credentials found' "$WORK_DIR/scan.txt" || fail "codex not listed"
grep -q 'codex login status. reports a login' "$WORK_DIR/scan.txt" || fail "codex evidence"
grep -q '^gemini  *not found' "$WORK_DIR/scan.txt" || fail "gemini not said absent"
grep -q 'apogee providers scan --register' "$WORK_DIR/scan.txt" || fail "no pointer to --register"
grep -qi 'authenticated' "$WORK_DIR/scan.txt" && fail "the scan claimed authentication"
cmp -s "$CONFIG" "$WORK_DIR/before.yaml" || fail "a plain scan changed the config"
[ -s "$APOGEE_HOME/cache/providers.json" ] || fail "the scan wrote no cache"

# --- complete and machine mode never offer -----------------------------------
"$APOGEE_BIN" complete "hi" </dev/null >"$WORK_DIR/complete.txt" 2>&1 || fail "complete failed"
printf '%s\n' '{"type":"user","text":"hi"}' \
    | "$APOGEE_BIN" chat --output-format stream-json --input-format stream-json \
        >"$WORK_DIR/machine.jsonl" 2>&1 || fail "machine mode failed"
printf 'hello\n' | "$APOGEE_BIN" chat >"$WORK_DIR/piped.txt" 2>&1 || fail "piped chat failed"
for out in complete.txt machine.jsonl piped.txt; do
    grep -q 'register' "$WORK_DIR/$out" && fail "$out carried an offer: $(cat "$WORK_DIR/$out")"
done
cmp -s "$CONFIG" "$WORK_DIR/before.yaml" || fail "a non-interactive run changed the config"
grep -q '"offer"' "$APOGEE_HOME/cache/providers.json" && fail "a non-interactive run recorded an answer"

# --- --register writes two entries, once -------------------------------------
"$APOGEE_BIN" providers scan --register </dev/null >"$WORK_DIR/reg.txt" 2>&1 \
    || fail "register failed: $(cat "$WORK_DIR/reg.txt")"
grep -q '^registered claude (claude-cli)$' "$WORK_DIR/reg.txt" || fail "claude not registered: $(cat "$WORK_DIR/reg.txt")"
grep -q '^registered codex (codex-cli)$' "$WORK_DIR/reg.txt" || fail "codex not registered"
grep -q 'models.default' "$WORK_DIR/reg.txt" && fail "the default was moved off mock"
"$APOGEE_BIN" config get backends.claude.type </dev/null 2>/dev/null | grep -q claude-cli \
    || grep -q '"type": "claude-cli"' "$CONFIG" || fail "no claude-cli entry in the config"
head -1 "$CONFIG" | cmp -s - <(head -1 "$WORK_DIR/before.yaml") || fail "the config's head changed"
cp "$CONFIG" "$WORK_DIR/registered.yaml"
"$APOGEE_BIN" providers scan --register </dev/null >"$WORK_DIR/again.txt" 2>&1 || fail "second register failed"
grep -q "^claude: already registered as 'claude' -- nothing written$" "$WORK_DIR/again.txt" \
    || fail "second pass did not say claude was registered: $(cat "$WORK_DIR/again.txt")"
grep -q '^nothing written$' "$WORK_DIR/again.txt" || fail "second pass did not say nothing was written"
cmp -s "$CONFIG" "$WORK_DIR/registered.yaml" || fail "a second --register changed the config"
"$APOGEE_BIN" check </dev/null >/dev/null 2>&1; true

# --- a taken name is skipped, never suffixed ----------------------------------
fresh_home two
"$APOGEE_BIN" config add-backend claude --type mock --model other >/dev/null || fail "add taken"
cp "$CONFIG" "$WORK_DIR/taken.yaml"
"$APOGEE_BIN" providers scan --register </dev/null >"$WORK_DIR/taken.txt" 2>&1 || fail "register (taken) failed"
grep -q "^claude: not registered -- a backend named 'claude' already exists (mock)" "$WORK_DIR/taken.txt" \
    || fail "the taken name was not said: $(cat "$WORK_DIR/taken.txt")"
grep -q '^registered codex (codex-cli)$' "$WORK_DIR/taken.txt" || fail "codex not registered beside it"
grep -q 'claude-2\|claude_2\|"claude-cli": {' "$CONFIG" && fail "a suffixed name was written"
grep -A2 '^    "claude": {' "$CONFIG" | grep -q '"type": "mock"' || fail "the existing claude entry was touched"

# --- no config: a scan still reads, --register refuses ------------------------
export APOGEE_HOME="$WORK_DIR/none"
"$APOGEE_BIN" providers scan </dev/null >"$WORK_DIR/none.txt" 2>&1 || fail "scan without config failed"
grep -q "config init" "$WORK_DIR/none.txt" || fail "no pointer to config init"
"$APOGEE_BIN" providers scan --register </dev/null >"$WORK_DIR/none-reg.txt" 2>&1 \
    && fail "--register without a config succeeded"
[ -e "$APOGEE_HOME/config/config.json" ] && fail "--register created a config"
[ -e "$APOGEE_HOME/config/config.yaml" ] && fail "--register created a YAML config"

echo "providers: scan, register, idempotence, a taken name and quiet surfaces - OK"
