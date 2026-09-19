#!/bin/sh
# The knowledge graph end to end, on the real binary, with the scripted mock
# as the extractor: a build over an ingested collection (--dry-run first,
# zero footprint), stats and show, the config's graph.enabled written through
# the one editor, a `complete --rag` turn whose status line reports the graph
# entities while the answer carries the entity of a chunk the query never
# matched, a captured knowledge record materialised as a decision node,
# `knowledge query --graph`, the cost policy's refusal, and delete.
#
# POSIX only, like the other shell checks.
set -eu

APOGEE_BIN="${1:?usage: graph_e2e.sh <apogee-binary> <work-dir>}"
WORK_DIR="${2:?usage: graph_e2e.sh <apogee-binary> <work-dir>}"

rm -rf "$WORK_DIR"
mkdir -p "$WORK_DIR/home" "$WORK_DIR/docs"
export APOGEE_HOME="$WORK_DIR/home"
unset ANTHROPIC_API_KEY OPENAI_API_KEY GEMINI_API_KEY GOOGLE_API_KEY
trap 'rm -rf "$WORK_DIR"' EXIT

fail() { echo "graph_e2e: $*" >&2; exit 1; }

# Two documents that share no vocabulary, linked only by the extractor's
# edge -- the acceptance fixture.
printf 'Atlas collects readings from the field probes every night.\n' > "$WORK_DIR/docs/atlas.md"
printf 'The warehouse keeps every record for seven years.\n' > "$WORK_DIR/docs/vault.md"

# The extractor answers the same extraction for every chunk: both entities and
# the edge, so the graph links the two documents. The answering model echoes
# the system prompt so the injected graph section is visible in the answer.
cat > "$WORK_DIR/extractor.json" <<'JSON'
{"turns": [{"text": "{\"entities\": [{\"name\": \"Atlas\", \"type\": \"system\", \"description\": \"collects readings\"}, {\"name\": \"Vault\", \"type\": \"system\", \"description\": \"the warehouse Atlas stores readings in\"}], \"relations\": [{\"source\": \"Atlas\", \"target\": \"Vault\", \"relation\": \"stores readings in\", \"description\": \"\"}]}"}]}
JSON
cat > "$WORK_DIR/echo.json" <<'JSON'
{"turns": [{"text": "SYSTEM WAS: {{system}}"}]}
JSON
cat > "$WORK_DIR/clerk.json" <<'JSON'
{"turns": [{"text": "{\"intent\": \"We route every probe reading through Atlas because the warehouse cannot ingest raw feeds.\", \"decision\": \"Atlas is the only writer.\", \"status\": \"shipped\", \"discipline\": \"eng\", \"downstream_link\": \"\", \"provenance\": {\"source\": \"meeting\", \"attribution\": \"Ada Lovelace\"}}"}]}
JSON

"$APOGEE_BIN" config init >/dev/null || fail "config init"
"$APOGEE_BIN" check --fix >/dev/null 2>&1 || fail "check --fix"
"$APOGEE_BIN" config add-backend extractor --type mock --model-path "$WORK_DIR/extractor.json" >/dev/null || fail "add-backend extractor"
"$APOGEE_BIN" config add-backend echo --type mock --model-path "$WORK_DIR/echo.json" >/dev/null || fail "add-backend echo"
"$APOGEE_BIN" config add-backend clerk --type mock --model-path "$WORK_DIR/clerk.json" >/dev/null || fail "add-backend clerk"
"$APOGEE_BIN" config add-backend paid --type anthropic --api-key sk-ant-test >/dev/null || fail "add-backend paid"
"$APOGEE_BIN" config set-default echo >/dev/null || fail "set-default"
CONFIG="$APOGEE_HOME/config/config.yaml"

"$APOGEE_BIN" embed ingest notes "$WORK_DIR/docs" </dev/null >"$WORK_DIR/ingest.txt" 2>&1 || fail "ingest: $(cat "$WORK_DIR/ingest.txt")"
cp "$CONFIG" "$WORK_DIR/config.before"

# --- the cost policy: a metered default is refused by fall-through ----------
# (Never run: the refusal comes before any provider call, so no network.)
"$APOGEE_BIN" config set-default paid >/dev/null || fail "set-default paid"
if "$APOGEE_BIN" graph build notes </dev/null >/dev/null 2>"$WORK_DIR/paid.err"; then
    fail "a metered default was accepted as the extractor"
fi
grep -q "never runs on a metered backend" "$WORK_DIR/paid.err" || fail "the refusal did not name the policy: $(cat "$WORK_DIR/paid.err")"
grep -q -- "-m paid" "$WORK_DIR/paid.err" || fail "the refusal did not name the way out: $(cat "$WORK_DIR/paid.err")"
"$APOGEE_BIN" config set-default echo >/dev/null || fail "set-default echo"
cp "$CONFIG" "$WORK_DIR/config.before"

# --- --dry-run: extractions printed, nothing stored, the config untouched --
"$APOGEE_BIN" graph build notes --dry-run -m extractor </dev/null >"$WORK_DIR/dry.txt" 2>"$WORK_DIR/dry.err" || fail "dry-run: $(cat "$WORK_DIR/dry.err")"
grep -q "Atlas (system) -- collects readings" "$WORK_DIR/dry.txt" || fail "no extraction printed: $(cat "$WORK_DIR/dry.txt")"
grep -q "nothing was stored" "$WORK_DIR/dry.txt" || fail "no dry-run summary: $(cat "$WORK_DIR/dry.txt")"
cmp -s "$CONFIG" "$WORK_DIR/config.before" || fail "--dry-run edited the config"
"$APOGEE_BIN" graph stats notes </dev/null >"$WORK_DIR/stats0.txt" 2>&1 || fail "stats before build"
grep -q "No graph built" "$WORK_DIR/stats0.txt" || fail "--dry-run stored something: $(cat "$WORK_DIR/stats0.txt")"

# --- the build: stamped, enabled through the editor ------------------------
"$APOGEE_BIN" graph build notes -m extractor </dev/null >"$WORK_DIR/build.txt" 2>"$WORK_DIR/build.err" || fail "build: $(cat "$WORK_DIR/build.err")"
grep -q "Graph build complete for \"notes\"" "$WORK_DIR/build.txt" || fail "no build summary: $(cat "$WORK_DIR/build.txt")"
grep -q "Files extracted:   2 of 2 planned" "$WORK_DIR/build.txt" || fail "not every file extracted: $(cat "$WORK_DIR/build.txt")"
grep -q "graph.enabled set on 'notes'" "$WORK_DIR/build.txt" || fail "graph.enabled was not set: $(cat "$WORK_DIR/build.txt")"
grep -q "^      enabled: true" "$CONFIG" || fail "the config does not carry graph.enabled: $(cat "$CONFIG")"
head -c "$(wc -c < "$WORK_DIR/config.before")" "$CONFIG" | cmp -s - "$WORK_DIR/config.before" || fail "the enable write changed bytes above the entry"
"$APOGEE_BIN" graph stats notes </dev/null >"$WORK_DIR/stats.txt" 2>&1 || fail "stats"
grep -q "Nodes:     2 (system 2)" "$WORK_DIR/stats.txt" || fail "stats: $(cat "$WORK_DIR/stats.txt")"
grep -q "Extractor: extractor" "$WORK_DIR/stats.txt" || fail "stats did not name the extractor"
"$APOGEE_BIN" graph show notes atlas </dev/null >"$WORK_DIR/show.txt" 2>&1 || fail "show"
grep -q "Atlas (system) -- 2 mention(s)" "$WORK_DIR/show.txt" || fail "show: $(cat "$WORK_DIR/show.txt")"
grep -q -- "-> Vault (system)" "$WORK_DIR/show.txt" || fail "show lacks the relation"
"$APOGEE_BIN" graph build notes -m extractor </dev/null >"$WORK_DIR/build2.txt" 2>&1 || fail "second build"
grep -q "Nothing to extract" "$WORK_DIR/build2.txt" || fail "a second build re-extracted: $(cat "$WORK_DIR/build2.txt")"

# --- retrieval-time expansion on the real binary ---------------------------
# The question matches atlas.md alone; the Vault shares no word with it and
# reaches the request only through the graph. The status line counts it.
"$APOGEE_BIN" complete --rag notes "field probes readings" </dev/null >"$WORK_DIR/turn.out" 2>"$WORK_DIR/turn.err" || fail "complete --rag: $(cat "$WORK_DIR/turn.err")"
grep -q "1 chunk(s) from 'notes'.*+2 graph entities" "$WORK_DIR/turn.err" || fail "the status line did not count the graph: $(cat "$WORK_DIR/turn.err")"
grep -q "\[Knowledge graph: notes\]" "$WORK_DIR/turn.out" || fail "the graph section did not reach the request: $(cat "$WORK_DIR/turn.out")"
grep -q "Vault (system): the warehouse" "$WORK_DIR/turn.out" || fail "the linked entity did not reach the request: $(cat "$WORK_DIR/turn.out")"
grep -q "seven years" "$WORK_DIR/turn.out" && fail "the other document's text was injected as a chunk"
# A question matching nothing lexically but naming an entity still expands.
"$APOGEE_BIN" complete --rag notes "Vault" </dev/null >"$WORK_DIR/turn2.out" 2>"$WORK_DIR/turn2.err" || fail "complete --rag by name"
grep -q "graph entities" "$WORK_DIR/turn2.err" || fail "an entity-name query did not expand: $(cat "$WORK_DIR/turn2.err")"

# --- a knowledge record becomes a decision node ------------------------------
"$APOGEE_BIN" knowledge capture -m clerk --db notes "Ada: route everything through Atlas? Bob: yes" </dev/null >"$WORK_DIR/cap.txt" 2>"$WORK_DIR/cap.err" || fail "capture: $(cat "$WORK_DIR/cap.err")"
id="$(sed -n 's/^Captured \(kr-[0-9TZ]*-[0-9a-f]*\)$/\1/p' "$WORK_DIR/cap.txt")"
[ -n "$id" ] || fail "no record id: $(cat "$WORK_DIR/cap.txt")"
"$APOGEE_BIN" graph build notes -m extractor </dev/null >"$WORK_DIR/build3.txt" 2>&1 || fail "build after capture: $(cat "$WORK_DIR/build3.txt")"
grep -q "Records as nodes:  1 decision node(s)" "$WORK_DIR/build3.txt" || fail "the record was not materialised: $(cat "$WORK_DIR/build3.txt")"
"$APOGEE_BIN" graph show notes "$id" </dev/null >"$WORK_DIR/showkr.txt" 2>&1 || fail "show the decision node"
grep -q "$id (decision, shipped, eng)" "$WORK_DIR/showkr.txt" || fail "the decision node lacks its markers: $(cat "$WORK_DIR/showkr.txt")"
grep -q "\[concerns\]" "$WORK_DIR/showkr.txt" || fail "no concerns edge: $(cat "$WORK_DIR/showkr.txt")"
"$APOGEE_BIN" knowledge query --db notes --graph "warehouse cannot ingest raw feeds" </dev/null >"$WORK_DIR/kq.txt" 2>"$WORK_DIR/kq.err" || fail "knowledge query --graph: $(cat "$WORK_DIR/kq.err")"
grep -q "Related (knowledge graph, " "$WORK_DIR/kq.txt" || fail "query --graph rendered nothing: $(cat "$WORK_DIR/kq.txt")"
grep -q -- "—\[concerns\]→ Atlas" "$WORK_DIR/kq.txt" || fail "the record's concerns edge is not in the walk: $(cat "$WORK_DIR/kq.txt")"
"$APOGEE_BIN" knowledge query --db notes --graph --json "warehouse cannot ingest raw feeds" </dev/null >"$WORK_DIR/kq.json" 2>&1 || fail "query --graph --json"
grep -q '"entities": ' "$WORK_DIR/kq.json" || fail "no graph object in the JSON: $(cat "$WORK_DIR/kq.json")"
# A docs turn reaches the decision through the shared entity, marked.
"$APOGEE_BIN" complete --rag notes "field probes readings" </dev/null >"$WORK_DIR/turn3.out" 2>/dev/null || fail "complete --rag after the record"
grep -q "$id (decision, shipped): Atlas is the only writer" "$WORK_DIR/turn3.out" || fail "the decision did not reach a docs turn: $(cat "$WORK_DIR/turn3.out")"

# --- delete clears the graph, keeps the chunks -------------------------------
"$APOGEE_BIN" graph delete notes </dev/null >"$WORK_DIR/delete.txt" 2>&1 || fail "delete"
grep -q "^Deleted the graph for \"notes\"" "$WORK_DIR/delete.txt" || fail "delete output: $(cat "$WORK_DIR/delete.txt")"
"$APOGEE_BIN" embed info notes </dev/null >"$WORK_DIR/info.txt" 2>&1 || fail "embed info after delete"
grep -Eq "chunks: +3" "$WORK_DIR/info.txt" || fail "delete took the chunks: $(cat "$WORK_DIR/info.txt")"

echo "apogee graph end-to-end: OK"
