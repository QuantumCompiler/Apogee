#!/bin/sh
# The knowledge layer end to end, on the real binary, on the scripted mock as
# the clerk: a capture stored as one chunk with the raw conversation archived
# privately and the attribution nowhere the text index can reach; --dry-run
# leaving zero footprint; overrides winning over the clerk; a supersede; the
# collection registered under embeddings: through the one config editor; the
# doctor's rows; a vendor-CLI backend refused by type; --from-chat over a
# saved session; and chat's /capture reaching the same core.
#
# POSIX only, like the other shell checks.
set -eu

APOGEE_BIN="${1:?usage: knowledge_e2e.sh <apogee-binary> <work-dir>}"
WORK_DIR="${2:?usage: knowledge_e2e.sh <apogee-binary> <work-dir>}"

rm -rf "$WORK_DIR"
mkdir -p "$WORK_DIR/home"
export APOGEE_HOME="$WORK_DIR/home"
unset ANTHROPIC_API_KEY OPENAI_API_KEY GEMINI_API_KEY GOOGLE_API_KEY
trap 'rm -rf "$WORK_DIR"' EXIT

fail() { echo "knowledge_e2e: $*" >&2; exit 1; }

# The clerk's answer: one conforming record, the attribution in its own field.
cat > "$WORK_DIR/clerk.json" <<'JSON'
{"turns": [{"text": "{\"intent\": \"We dropped the cancel button because testers kept mistaking it for back.\", \"decision\": \"Remove the cancel button from checkout.\", \"status\": \"shipped\", \"discipline\": \"ux\", \"downstream_link\": \"\", \"provenance\": {\"source\": \"meeting\", \"attribution\": \"Ada Lovelace\"}}"}]}
JSON
cat > "$WORK_DIR/prose.json" <<'JSON'
{"turns": [{"text": "I cannot produce JSON."}, {"text": "STILL-PROSE"}]}
JSON

"$APOGEE_BIN" config init >/dev/null || fail "config init"
"$APOGEE_BIN" check --fix >/dev/null 2>&1 || fail "check --fix"
[ -d "$APOGEE_HOME/knowledge" ] || fail "the knowledge row was not seeded"
"$APOGEE_BIN" config add-backend clerk --type mock --model-path "$WORK_DIR/clerk.json" >/dev/null || fail "add-backend clerk"
"$APOGEE_BIN" config add-backend prose --type mock --model-path "$WORK_DIR/prose.json" >/dev/null || fail "add-backend prose"
"$APOGEE_BIN" config add-backend vendor --type claude-cli >/dev/null || fail "add-backend vendor"
"$APOGEE_BIN" config set-default clerk >/dev/null || fail "set-default"
CONFIG="$APOGEE_HOME/config/config.yaml"
cp "$CONFIG" "$WORK_DIR/config.before"

# --- --dry-run: the clerk runs, nothing is written ---------------------------
"$APOGEE_BIN" knowledge capture --dry-run "we talked about the cancel button" </dev/null >"$WORK_DIR/dry.txt" 2>"$WORK_DIR/dry.err" || fail "dry-run: $(cat "$WORK_DIR/dry.err")"
grep -q "^Draft (not stored)" "$WORK_DIR/dry.txt" || fail "no draft header: $(cat "$WORK_DIR/dry.txt")"
grep -q 'Would store in "knowledge" (retriever: lexical)' "$WORK_DIR/dry.txt" || fail "no store decision: $(cat "$WORK_DIR/dry.txt")"
grep -q "Attribution: Ada Lovelace" "$WORK_DIR/dry.txt" || fail "the draft did not show the clerk's fields"
[ ! -e "$APOGEE_HOME/embeddings/knowledge.db" ] || fail "--dry-run created the collection"
[ ! -e "$APOGEE_HOME/knowledge/raw" ] || fail "--dry-run created the raw archive"
cmp -s "$CONFIG" "$WORK_DIR/config.before" || fail "--dry-run edited the config"
"$APOGEE_BIN" kn capture --dry-run --json "x" </dev/null >"$WORK_DIR/dry.json" 2>/dev/null || fail "kn alias / --json dry-run"
grep -q '"draft": true' "$WORK_DIR/dry.json" || fail "the JSON draft is not marked: $(cat "$WORK_DIR/dry.json")"
grep -q '"id": ""' "$WORK_DIR/dry.json" || fail "a draft carries an id"

# --- a real capture: stored, archived, registered, the name unsearchable -----
"$APOGEE_BIN" knowledge capture "Ada: drop the cancel button? Bob: yes, testers mistake it for back" </dev/null >"$WORK_DIR/cap.txt" 2>"$WORK_DIR/cap.err" || fail "capture: $(cat "$WORK_DIR/cap.err")"
grep -q "^Captured kr-" "$WORK_DIR/cap.txt" || fail "no Captured line: $(cat "$WORK_DIR/cap.txt")"
id="$(sed -n 's/^Captured \(kr-[0-9TZ]*-[0-9a-f]*\)$/\1/p' "$WORK_DIR/cap.txt")"
[ -n "$id" ] || fail "could not read the id: $(cat "$WORK_DIR/cap.txt")"
grep -q 'Stored in "knowledge" (retriever: lexical)' "$WORK_DIR/cap.txt" || fail "no store line"
grep -q "registered 'knowledge' in" "$WORK_DIR/cap.txt" || fail "the collection was not registered"
grep -q "^  knowledge:" "$CONFIG" || fail "no embeddings entry for knowledge in the config"
[ -f "$APOGEE_HOME/embeddings/knowledge.db" ] || fail "no collection file"
[ -f "$APOGEE_HOME/knowledge/raw/$id.md" ] || fail "no raw archive for $id"
grep -q "Bob: yes, testers mistake it for back" "$APOGEE_HOME/knowledge/raw/$id.md" || fail "the archive is not the raw conversation"
[ "$(stat -f '%Lp' "$APOGEE_HOME/knowledge/raw" 2>/dev/null || stat -c '%a' "$APOGEE_HOME/knowledge/raw")" = "700" ] || fail "the raw archive is not private"
# The text index holds the reasoning and not the name.
"$APOGEE_BIN" embed query knowledge "why was the cancel button removed" </dev/null >"$WORK_DIR/query.txt" 2>&1 || fail "embed query: $(cat "$WORK_DIR/query.txt")"
grep -q "$id" "$WORK_DIR/query.txt" || fail "the record is not findable by text: $(cat "$WORK_DIR/query.txt")"
"$APOGEE_BIN" embed query knowledge "Lovelace" </dev/null >"$WORK_DIR/name.txt" 2>&1 || true
grep -q "no matches" "$WORK_DIR/name.txt" || fail "the attribution reached the index: $(cat "$WORK_DIR/name.txt")"

# --- overrides beat the clerk; --supersedes flips the earlier record ---------
"$APOGEE_BIN" knowledge capture --status rejected --link PROJ-42 --discipline eng --source review --supersedes "$id" --json "second thoughts" </dev/null >"$WORK_DIR/second.json" 2>"$WORK_DIR/second.err" || fail "capture with overrides: $(cat "$WORK_DIR/second.err")"
grep -q '"status": "rejected"' "$WORK_DIR/second.json" || fail "--status did not win: $(cat "$WORK_DIR/second.json")"
grep -q '"downstream_link": "PROJ-42"' "$WORK_DIR/second.json" || fail "--link did not win"
grep -q '"discipline": "eng"' "$WORK_DIR/second.json" || fail "--discipline did not win"
grep -q '"source": "review"' "$WORK_DIR/second.json" || fail "--source did not win"
grep -q "\"supersedes\": \"$id\"" "$WORK_DIR/second.json" || fail "--supersedes was not recorded"
grep -q '"registered": false' "$WORK_DIR/second.json" || fail "a second capture re-registered the collection"
grep -q "notes" "$WORK_DIR/second.json" && fail "a supersede of an existing record left a note: $(cat "$WORK_DIR/second.json")"
"$APOGEE_BIN" knowledge capture --supersedes kr-nope --json "third" </dev/null >"$WORK_DIR/third.json" 2>/dev/null || fail "capture with a missing supersedes target must still store"
grep -q "no such record" "$WORK_DIR/third.json" || fail "a missing supersedes target was not reported: $(cat "$WORK_DIR/third.json")"

# --- the doctor sees the collection and the archive --------------------------
"$APOGEE_BIN" check </dev/null >"$WORK_DIR/check.txt" 2>&1 || fail "check: $(cat "$WORK_DIR/check.txt")"
grep -q "collection: knowledge  3 record(s), text index ok" "$WORK_DIR/check.txt" || fail "the doctor did not count the records: $(cat "$WORK_DIR/check.txt")"
grep -q "raw archive  private (0700), 3 conversation(s)" "$WORK_DIR/check.txt" || fail "the doctor did not see the archive: $(cat "$WORK_DIR/check.txt")"

# --- a vendor CLI is refused by type; a clerk that never conforms stores nothing
if "$APOGEE_BIN" knowledge capture -m vendor "x" </dev/null >/dev/null 2>"$WORK_DIR/vendor.err"; then
    fail "a vendor-CLI backend was accepted as the clerk"
fi
grep -q "vendor-CLI backend" "$WORK_DIR/vendor.err" || fail "the refusal did not say why: $(cat "$WORK_DIR/vendor.err")"
if "$APOGEE_BIN" knowledge capture -m prose "x" </dev/null >/dev/null 2>"$WORK_DIR/prose.err"; then
    fail "a clerk that never conformed was accepted"
fi
grep -q "did not return a record" "$WORK_DIR/prose.err" || fail "the failure did not name the clerk: $(cat "$WORK_DIR/prose.err")"
grep -q "collection: knowledge  3 record(s)" "$WORK_DIR/check.txt" || fail "a failed capture stored something"
if "$APOGEE_BIN" knowledge capture "x" --status maybe </dev/null >/dev/null 2>&1; then
    fail "a bad --status was accepted"
fi

# --- chat: /capture through the same core, and --from-chat over the session --
printf 'should we drop the cancel button?\n/capture rejected\n/exit\n' | "$APOGEE_BIN" chat -m clerk >"$WORK_DIR/chat.out" 2>"$WORK_DIR/chat.err" || fail "chat /capture: $(cat "$WORK_DIR/chat.err")"
grep -q "captured kr-.*\[rejected\]" "$WORK_DIR/chat.err" || fail "/capture did not report a record: $(cat "$WORK_DIR/chat.err")"
"$APOGEE_BIN" check </dev/null >"$WORK_DIR/check2.txt" 2>&1 || fail "check after chat"
grep -q "collection: knowledge  4 record(s)" "$WORK_DIR/check2.txt" || fail "/capture did not store: $(cat "$WORK_DIR/check2.txt")"
chat_id="$("$APOGEE_BIN" chats list </dev/null 2>/dev/null | sed -n '1s/^\([^ ]*\).*/\1/p')"
[ -n "$chat_id" ] || fail "no saved chat to capture from"
"$APOGEE_BIN" knowledge capture --from-chat "$chat_id" --json </dev/null >"$WORK_DIR/fromchat.json" 2>"$WORK_DIR/fromchat.err" || fail "--from-chat: $(cat "$WORK_DIR/fromchat.err")"
grep -q '"source": "chat"' "$WORK_DIR/fromchat.json" || fail "--from-chat did not mark the source: $(cat "$WORK_DIR/fromchat.json")"
from_id="$(sed -n 's/.*"id": "\(kr-[0-9TZ]*-[0-9a-f]*\)".*/\1/p' "$WORK_DIR/fromchat.json" | head -1)"
[ -n "$from_id" ] || fail "no id in the --from-chat result"
grep -q "^User: should we drop the cancel button?" "$APOGEE_HOME/knowledge/raw/$from_id.md" || fail "the archived transcript is not the session's words: $(cat "$APOGEE_HOME/knowledge/raw/$from_id.md")"

echo "apogee knowledge end-to-end: OK"
