#!/usr/bin/env bash
# Provider surfacing (28c) on the real binary, with fake vendor CLIs.
#
#   - a registered `claude-cli` backend and no `claude` anywhere on PATH:
#     `check` WARNS on it with the use-time error's remedy -- never `ok` --
#     and says the providers were never scanned, probing nothing;
#   - the binaries put back: `check --refresh-providers` scans, and the row
#     reports the tier honestly (credentials found, the version);
#   - `models list` fills STATE with tier words and VERIFIED with `no turn
#     yet`; one real turn through a fake `codex` (the recorded `codex exec
#     --json` fixture) and VERIFIED carries today's date, `check` and
#     `models info` say verified;
#   - check and models list spend nothing: the fake codex logs every `exec`,
#     and only the one turn ran one.
#
# POSIX only.

set -uo pipefail

APOGEE_BIN="${1:?usage: provider_surfacing_e2e.sh <apogee-binary> <work-dir> <codex-fixture>}"
WORK_DIR="${2:?usage: provider_surfacing_e2e.sh <apogee-binary> <work-dir> <codex-fixture>}"
FIXTURE="${3:?usage: provider_surfacing_e2e.sh <apogee-binary> <work-dir> <codex-fixture>}"

rm -rf "$WORK_DIR"
mkdir -p "$WORK_DIR/bin" "$WORK_DIR/home" "$WORK_DIR/empty"
export HOME="$WORK_DIR/home"
export APOGEE_HOME="$WORK_DIR/apogee"
trap 'rm -rf "$WORK_DIR"' EXIT
unset ANTHROPIC_API_KEY OPENAI_API_KEY GEMINI_API_KEY GOOGLE_API_KEY

fail() { echo "provider_surfacing: $*" >&2; exit 1; }

LOG="$WORK_DIR/codex.log"
cat >"$WORK_DIR/bin/claude" <<'SCRIPT'
#!/bin/sh
echo '9.9.9 (Claude Code)'
SCRIPT
cat >"$WORK_DIR/bin/codex" <<SCRIPT
#!/bin/sh
case "\$1" in
    --version) echo 'codex-cli 9.9.9' ;;
    login) exit 0 ;;
    exec) echo "exec" >> "$LOG"; cat "$FIXTURE" ;;
esac
SCRIPT
chmod +x "$WORK_DIR/bin/claude" "$WORK_DIR/bin/codex"
: >"$HOME/.claude.json"

# Setup on the normal PATH; the checks below choose theirs.
"$APOGEE_BIN" config init >/dev/null || fail "config init"
"$APOGEE_BIN" config add-backend mock --type mock --model mock-1 >/dev/null || fail "add mock"
"$APOGEE_BIN" config set-default mock >/dev/null || fail "set-default"
"$APOGEE_BIN" config add-backend claude --type claude-cli >/dev/null || fail "add claude"
"$APOGEE_BIN" config add-backend codex --type codex-cli >/dev/null || fail "add codex"

# --- stripped PATH: a warning with the remedy, never ok ------------------------
PATH="$WORK_DIR/empty:/usr/bin:/bin" "$APOGEE_BIN" check --no-color </dev/null >"$WORK_DIR/strip.txt" 2>&1
grep -q '^ warn  backend: claude  claude-cli -- .claude. was not found on PATH$' "$WORK_DIR/strip.txt" \
    || fail "no warning for the missing claude: $(cat "$WORK_DIR/strip.txt")"
grep -q "run: Install the Claude CLI and log in, or set 'binary' on this backend to its full path" \
    "$WORK_DIR/strip.txt" || fail "the remedy was not the use-time error's"
grep -q '^  ok   backend: claude' "$WORK_DIR/strip.txt" && fail "the missing claude still read ok"
grep -q '^  ok   scan  not scanned yet' "$WORK_DIR/strip.txt" || fail "an unscanned machine was not said"
grep -q 'check --refresh-providers' "$WORK_DIR/strip.txt" || fail "no way to scan named"
[ -e "$APOGEE_HOME/cache/providers.json" ] && fail "a plain check wrote the provider cache"
PATH="$WORK_DIR/empty:/usr/bin:/bin" "$APOGEE_BIN" models list </dev/null >"$WORK_DIR/strip-list.txt" 2>&1
grep -E '^claude +claude-cli .* no binary +no turn yet' "$WORK_DIR/strip-list.txt" >/dev/null \
    || fail "models list did not say no binary: $(cat "$WORK_DIR/strip-list.txt")"

export PATH="$WORK_DIR/bin:/usr/bin:/bin"

# --- the binaries there: the tier, honestly ------------------------------------
"$APOGEE_BIN" check --no-color </dev/null >"$WORK_DIR/cheap.txt" 2>&1
grep -q '^  ok   backend: claude  claude-cli -- credentials found (~/.claude.json exists); version not asked yet$' \
    "$WORK_DIR/cheap.txt" || fail "the cheap tier was wrong: $(cat "$WORK_DIR/cheap.txt")"
"$APOGEE_BIN" check --no-color --refresh-providers </dev/null >"$WORK_DIR/scan.txt" 2>&1
grep -q '^  ok   backend: claude  claude-cli -- credentials found (~/.claude.json exists); 9.9.9 (Claude Code)$' \
    "$WORK_DIR/scan.txt" || fail "the scanned tier was wrong: $(cat "$WORK_DIR/scan.txt")"
grep -q 'backend: codex  codex-cli -- credentials found (`codex login status` reports a login); codex-cli 9.9.9' \
    "$WORK_DIR/scan.txt" || fail "codex's tier was wrong"
grep -q '^  ok   scan  last scanned ' "$WORK_DIR/scan.txt" || fail "the scan's time was not said"
grep -qi 'authenticated' "$WORK_DIR/scan.txt" && fail "check claimed authentication"

"$APOGEE_BIN" models list </dev/null >"$WORK_DIR/list.txt" 2>&1
grep -E '^codex +codex-cli .* credentials found +no turn yet' "$WORK_DIR/list.txt" >/dev/null \
    || fail "codex's row lacked its tier: $(cat "$WORK_DIR/list.txt")"
[ -s "$LOG" ] && fail "check or models list ran a turn: $(cat "$LOG")"

# --- one real turn: verified, today ------------------------------------------------
"$APOGEE_BIN" complete -m codex "say hello" </dev/null >"$WORK_DIR/turn.txt" 2>&1 \
    || fail "the codex turn failed: $(cat "$WORK_DIR/turn.txt")"
grep -q hello "$WORK_DIR/turn.txt" || fail "the turn did not answer"
[ "$(grep -c exec "$LOG")" -eq 1 ] || fail "expected exactly one exec: $(cat "$LOG")"
TODAY=$(date +%Y-%m-%d)
"$APOGEE_BIN" models list </dev/null >"$WORK_DIR/after.txt" 2>&1
grep -E "^codex +codex-cli .* credentials found +$TODAY" "$WORK_DIR/after.txt" >/dev/null \
    || fail "VERIFIED did not carry today: $(cat "$WORK_DIR/after.txt")"
grep -E '^claude +claude-cli .* no turn yet' "$WORK_DIR/after.txt" >/dev/null \
    || fail "claude was marked verified without a turn"
"$APOGEE_BIN" check --no-color </dev/null >"$WORK_DIR/verified.txt" 2>&1
grep -q "backend: codex  codex-cli -- verified -- answered a turn on $TODAY" "$WORK_DIR/verified.txt" \
    || fail "check did not say verified: $(cat "$WORK_DIR/verified.txt")"
"$APOGEE_BIN" models info codex </dev/null >"$WORK_DIR/info.txt" 2>&1 || fail "models info failed"
grep -q "^verified:     last answered a turn on $TODAY (codex)$" "$WORK_DIR/info.txt" \
    || fail "models info lacked the verified line: $(cat "$WORK_DIR/info.txt")"
[ "$(grep -c exec "$LOG")" -eq 1 ] || fail "check, models list or info spent a turn"

echo "provider_surfacing: the missing binary warned, tiers in words, a turn verified - OK"
