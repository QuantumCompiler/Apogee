#!/bin/sh
# `apogee analyze` end to end, on the real binary, on the mock backend: the
# bundled agents against a fixture repository -- schema-conforming output
# rendered with human_summary last, saved quietly under analyses/<agent>/ and
# printed when piped; --branch reviewing a ref that is NOT checked out, with
# the refs decided by the flags and never by the input text; a non-conforming
# answer retried once and, failing twice, delivered raw and flagged; a
# vendor-CLI backend refused by type; and `chat --branch` reaching the same
# git defaults through the same plumbing.
#
# POSIX only, like the other shell checks. Needs git.
set -eu

APOGEE_BIN="${1:?usage: analyze_e2e.sh <apogee-binary> <work-dir>}"
WORK_DIR="${2:?usage: analyze_e2e.sh <apogee-binary> <work-dir>}"

if ! command -v git >/dev/null 2>&1; then
    echo "analyze_e2e: git not found; skipping"
    exit 0
fi

rm -rf "$WORK_DIR"
mkdir -p "$WORK_DIR/home" "$WORK_DIR/repo"
export APOGEE_HOME="$WORK_DIR/home"
unset ANTHROPIC_API_KEY OPENAI_API_KEY GEMINI_API_KEY GOOGLE_API_KEY
export GIT_CONFIG_GLOBAL=/dev/null GIT_CONFIG_SYSTEM=/dev/null
export GIT_AUTHOR_NAME=t GIT_AUTHOR_EMAIL=t@example.com GIT_COMMITTER_NAME=t GIT_COMMITTER_EMAIL=t@example.com
trap 'rm -rf "$WORK_DIR"' EXIT

fail() { echo "analyze_e2e: $*" >&2; exit 1; }

# --- the fixture repository: main, plus a feature branch NOT checked out ------
cd "$WORK_DIR/repo"
git init -q -b main
echo hello > README.md
git add . && git commit -q -m "init"
git checkout -q -b feature
echo 'password = "hunter2"' > secret.py
git add . && git commit -q -m "add secret"
git checkout -q main

# --- the scripted mock: call git_diff with NO refs, then answer the schema ---
# The answer carries the tool result verbatim, which is how the run proves
# which refs the tools compared.
cat > "$WORK_DIR/review.json" <<'JSON'
{"turns": [
  {"text": "", "tool_calls": [{"name": "git_diff", "arguments": {}}]},
  {"text": "{\"summary\": \"reviewed\", \"findings\": [], \"verdict\": \"no_security_concerns\", \"verdict_rationale\": \"clean\", \"human_summary\": {{last_tool_result:json}}}"}
]}
JSON
cat > "$WORK_DIR/retry.json" <<'JSON'
{"turns": [{"text": "not json at all"},
           {"text": "{\"summary\": \"fixed\", \"findings\": [], \"verdict\": \"no_security_concerns\", \"verdict_rationale\": \"ok\", \"human_summary\": \"SECOND-TRY\"}"}]}
JSON
cat > "$WORK_DIR/prose.json" <<'JSON'
{"turns": [{"text": "I cannot produce JSON."}, {"text": "STILL-PROSE"}]}
JSON

"$APOGEE_BIN" config init >/dev/null || fail "config init"
"$APOGEE_BIN" check --fix >/dev/null 2>&1 || fail "check --fix"
[ -f "$APOGEE_HOME/prompts/security-review.txt" ] || fail "the bundled prompt was not seeded"
[ -f "$APOGEE_HOME/schemas/merge-request-output.json" ] || fail "the bundled schema was not seeded"
"$APOGEE_BIN" config add-backend review --type mock --model-path "$WORK_DIR/review.json" >/dev/null || fail "add-backend review"
"$APOGEE_BIN" config add-backend retry --type mock --model-path "$WORK_DIR/retry.json" >/dev/null || fail "add-backend retry"
"$APOGEE_BIN" config add-backend prose --type mock --model-path "$WORK_DIR/prose.json" >/dev/null || fail "add-backend prose"
"$APOGEE_BIN" config add-backend vendor --type claude-cli >/dev/null || fail "add-backend vendor"
"$APOGEE_BIN" config set-default review >/dev/null || fail "set-default"

# --- --list needs no backend -------------------------------------------------
"$APOGEE_BIN" analyze --list </dev/null >"$WORK_DIR/list.txt" 2>&1 || fail "analyze --list"
grep -q "security-review" "$WORK_DIR/list.txt" || fail "--list does not name the bundled agents"
grep -q "release-notes" "$WORK_DIR/list.txt" || fail "--list does not name release-notes"

# --- --branch reviews a ref that is NOT checked out, from the flags ---------
# The input text names a different branch: it must change nothing.
"$APOGEE_BIN" analyze --agent security-review --branch feature --no-fetch --json "please review main against nothing" </dev/null >"$WORK_DIR/branch.json" 2>"$WORK_DIR/branch.err" || fail "analyze --branch: $(cat "$WORK_DIR/branch.err")"
grep -q "Review: main...feature" "$WORK_DIR/branch.json" || fail "the diff was not main...feature: $(cat "$WORK_DIR/branch.json")"
grep -q "secret.py" "$WORK_DIR/branch.json" || fail "the reviewed diff does not carry the feature branch's file"
grep -q '"conforms": false' "$WORK_DIR/branch.json" && fail "a conforming answer was flagged"
[ "$(git -C "$WORK_DIR/repo" branch --show-current)" = "main" ] || fail "the review checked the branch out"
grep -q "^Saved: " "$WORK_DIR/branch.err" || fail "a named agent's run was not saved: $(cat "$WORK_DIR/branch.err")"
ls "$APOGEE_HOME/analyses/security-review/"*.json >/dev/null 2>&1 || fail "the report did not land under analyses/security-review/"
# A remote-only ref with fetching refused is a clear error, not a guess.
if "$APOGEE_BIN" analyze --agent security-review --branch nowhere --no-fetch --json "x" </dev/null >"$WORK_DIR/nowhere.json" 2>/dev/null; then
    grep -q "fetching is disabled" "$WORK_DIR/nowhere.json" || fail "an absent ref under --no-fetch was not refused with the reason: $(cat "$WORK_DIR/nowhere.json")"
fi

# --- rendered Markdown, printed when piped, human_summary last --------------
"$APOGEE_BIN" analyze --agent security-review --branch feature --no-fetch </dev/null >"$WORK_DIR/rendered.md" 2>"$WORK_DIR/rendered.err" || fail "analyze rendered: $(cat "$WORK_DIR/rendered.err")"
grep -q "^## Summary" "$WORK_DIR/rendered.md" || fail "no Summary section: $(cat "$WORK_DIR/rendered.md")"
grep -q "^## Human Summary" "$WORK_DIR/rendered.md" || fail "no Human Summary section"
last_heading="$(grep '^## ' "$WORK_DIR/rendered.md" | tail -1)"
[ "$last_heading" = "## Human Summary" ] || fail "human_summary is not last: $last_heading"
saved="$(sed -n 's/^Saved: //p' "$WORK_DIR/rendered.err")"
[ -f "$saved" ] || fail "the Saved: line names no file: $(cat "$WORK_DIR/rendered.err")"
case "$saved" in
    *:*) fail "the saved filename carries a colon: $saved" ;;
    "$APOGEE_HOME/analyses/security-review/security-review-"*.md) ;;
    *) fail "unexpected save path: $saved" ;;
esac
# Ad hoc: a prompt file, no agent, nothing saved, the answer printed.
printf 'Answer briefly.\n' > "$WORK_DIR/adhoc.txt"
"$APOGEE_BIN" analyze --prompt "$WORK_DIR/adhoc.txt" -m retry "x" </dev/null >"$WORK_DIR/adhoc.out" 2>"$WORK_DIR/adhoc.err" || fail "ad hoc analyze"
grep -q "not json at all" "$WORK_DIR/adhoc.out" || fail "the ad hoc answer was not printed: $(cat "$WORK_DIR/adhoc.out")"
grep -q "^Saved: " "$WORK_DIR/adhoc.err" && fail "an ad hoc run was saved"

# --- the validator: one retry; two misses delivered raw and flagged ---------
"$APOGEE_BIN" analyze --agent security-review -m retry --json "x" </dev/null >"$WORK_DIR/retry.out" 2>"$WORK_DIR/retry.err" || fail "analyze retry"
grep -q "SECOND-TRY" "$WORK_DIR/retry.out" || fail "the corrected second answer was not delivered: $(cat "$WORK_DIR/retry.out")"
grep -q '"conforms": false' "$WORK_DIR/retry.out" && fail "a conforming retry was flagged"
"$APOGEE_BIN" analyze --agent security-review -m prose --json "x" </dev/null >"$WORK_DIR/prose.out" 2>"$WORK_DIR/prose.err" || fail "analyze prose (must not fail the run)"
grep -q '"conforms": false' "$WORK_DIR/prose.out" || fail "two misses were not flagged: $(cat "$WORK_DIR/prose.out")"
grep -q "STILL-PROSE" "$WORK_DIR/prose.out" || fail "the raw answer was dropped"
grep -q "conforms: false" "$WORK_DIR/prose.err" || fail "the warning did not reach stderr"

# --- a vendor-CLI backend is refused by type, before anything is spawned -----
if "$APOGEE_BIN" analyze --agent security-review -m vendor "x" </dev/null >/dev/null 2>"$WORK_DIR/vendor.err"; then
    fail "a vendor-CLI backend was accepted"
fi
grep -q "vendor-CLI backend" "$WORK_DIR/vendor.err" || fail "the refusal did not say why: $(cat "$WORK_DIR/vendor.err")"
grep -q "claude-cli" "$WORK_DIR/vendor.err" || fail "the refusal did not name the type"

# --- agents create: runnable at once; delete --purge ------------------------
"$APOGEE_BIN" agents create summarizer --description "Sums" --tools none </dev/null >/dev/null || fail "agents create"
"$APOGEE_BIN" analyze --list </dev/null | grep -q "summarizer" || fail "the created agent is not listed"
"$APOGEE_BIN" analyze --agent summarizer -m retry --json "x" </dev/null >"$WORK_DIR/sum.out" 2>/dev/null || fail "the created agent does not run"
"$APOGEE_BIN" check </dev/null >"$WORK_DIR/check.txt" 2>&1 || fail "check after create: $(cat "$WORK_DIR/check.txt")"
grep -q "agent: summarizer" "$WORK_DIR/check.txt" || fail "the doctor has no Agents row for the created agent"
"$APOGEE_BIN" agents delete summarizer --purge </dev/null >/dev/null || fail "agents delete"
[ ! -f "$APOGEE_HOME/prompts/summarizer.txt" ] || fail "--purge left the prompt behind"

# --- chat --branch: the same plumbing, the same diff, the same note ---------
printf 'review it\n' | "$APOGEE_BIN" chat --tools --branch feature --no-fetch >"$WORK_DIR/chat.out" 2>/dev/null || fail "chat --branch"
grep -q "Review: main...feature" "$WORK_DIR/chat.out" || fail "chat --branch did not review main...feature: $(cat "$WORK_DIR/chat.out")"
# The review note reaches the model as a system message on every turn, and
# never the transcript: a mock that echoes its system prompt shows it.
cat > "$WORK_DIR/note.json" <<'JSON'
{"turns": [{"text": "SYSTEM<<{{system}}>>"}]}
JSON
"$APOGEE_BIN" config add-backend note --type mock --model-path "$WORK_DIR/note.json" >/dev/null || fail "add-backend note"
printf 'hi\n' | "$APOGEE_BIN" chat -m note --tools --branch feature --base main --no-fetch >"$WORK_DIR/note.out" 2>/dev/null || fail "chat note"
grep -q "Git review context: review feature compared to main" "$WORK_DIR/note.out" || fail "the review note did not reach the model: $(cat "$WORK_DIR/note.out")"
# Resumed WITHOUT --branch, the model no longer sees the note: it rode the
# request, never the transcript, so a resumed session gets what its own
# flags say. (The echo mock's answer holds the old note; the system prompt
# it sees now must not.)
printf 'again\n' | "$APOGEE_BIN" chat -m note --tools --continue --no-fetch >"$WORK_DIR/resume.out" 2>/dev/null || fail "chat --continue"
grep -q "SYSTEM<<" "$WORK_DIR/resume.out" || fail "the resumed turn did not answer: $(cat "$WORK_DIR/resume.out")"
if sed -n 's/.*SYSTEM<<\(.*\)>>.*/\1/p' "$WORK_DIR/resume.out" | grep -q "Git review context"; then
    fail "the review note was persisted into the transcript: $(cat "$WORK_DIR/resume.out")"
fi

echo "apogee analyze end-to-end: OK"
