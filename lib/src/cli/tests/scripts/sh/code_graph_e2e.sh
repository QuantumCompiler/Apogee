#!/bin/sh
# The code graph end to end on the real binary (27k). First, the model-free
# pass on a sandbox with ZERO backends, no key and the network pointed at a
# closed port: `graph build --source` over a copy of the Python fixture tree
# (an unsupported file named and counted, a vendored directory left out),
# `stats` with the extracted/inferred split, `show` walking callers across
# files to the line, an edit re-parsed alone by `update` and the result equal
# to a fresh build, a same-bytes rewrite parsing nothing, `communities`
# clustering with the summaries reported absent, and `dedupe` saying what
# each layer had. Then, with a scripted mock to answer, retrieval-time
# expansion over a graph holding code beside a collection: a lexical turn
# naming a function injects its code entity, labelled `file:line`, and the
# parsed relation marked extracted.
#
# POSIX only, like the other shell checks.
set -eu

APOGEE_BIN="${1:?usage: code_graph_e2e.sh <apogee-binary> <work-dir> <fixtures-dir>}"
WORK_DIR="${2:?usage: code_graph_e2e.sh <apogee-binary> <work-dir> <fixtures-dir>}"
FIXTURES="${3:?usage: code_graph_e2e.sh <apogee-binary> <work-dir> <fixtures-dir>}"

rm -rf "$WORK_DIR"
mkdir -p "$WORK_DIR/home" "$WORK_DIR/docs"
export APOGEE_HOME="$WORK_DIR/home"
unset ANTHROPIC_API_KEY OPENAI_API_KEY GEMINI_API_KEY GOOGLE_API_KEY
# Offline: anything that tried the network would meet a closed port.
export http_proxy=http://127.0.0.1:9 https_proxy=http://127.0.0.1:9 \
    HTTP_PROXY=http://127.0.0.1:9 HTTPS_PROXY=http://127.0.0.1:9 ALL_PROXY=http://127.0.0.1:9
# The source tree lives outside any repository: the work directory sits in
# the build tree, which git ignores -- and inside a repository a source
# tree is read the way git reads it, so an ignored one offers nothing.
SRC_ROOT=$(mktemp -d "${TMPDIR:-/tmp}/apogee-code-graph.XXXXXX")
trap 'rm -rf "$WORK_DIR" "$SRC_ROOT"' EXIT

fail() { echo "code_graph_e2e: $*" >&2; exit 1; }

# The tree: the Python fixture, plus a file no grammar parses and a vendored
# directory.
cp -R "$FIXTURES/code_graph/python" "$SRC_ROOT/src"
printf 'print("no vendored grammar parses Lua")\n' > "$SRC_ROOT/src/tool.lua"
mkdir -p "$SRC_ROOT/src/third_party/dep"
printf 'def vendored():\n    pass\n' > "$SRC_ROOT/src/third_party/dep/dep.py"

"$APOGEE_BIN" config init >/dev/null || fail "config init"
"$APOGEE_BIN" check --fix >/dev/null 2>&1 || fail "check --fix"
CONFIG="$APOGEE_HOME/config/config.yaml"
# Zero backends: the template configures none.
if "$APOGEE_BIN" config get backends </dev/null 2>/dev/null | grep -q "type:"; then
    fail "the sandbox has a backend configured"
fi

# --- a tree git ignores offers nothing, and says so ---------------------------
# (When the work directory sits inside a repository's ignored build tree.)
mkdir -p "$WORK_DIR/ignored"
cp -R "$FIXTURES/code_graph/python/pkg" "$WORK_DIR/ignored/pkg"
if git -C "$WORK_DIR/ignored" check-ignore -q . 2>/dev/null; then
    "$APOGEE_BIN" graph build --source "$WORK_DIR/ignored" --graph ignored --dry-run </dev/null >"$WORK_DIR/ignored.txt" 2>&1 || fail "ignored dry run: $(cat "$WORK_DIR/ignored.txt")"
    grep -q "offers no file to read" "$WORK_DIR/ignored.txt" || fail "an ignored tree was not said: $(cat "$WORK_DIR/ignored.txt")"
fi

# --- build: no model, the skipped file named, the vendored one left out ------
"$APOGEE_BIN" graph build --source "$SRC_ROOT/src" --graph code </dev/null >"$WORK_DIR/build.txt" 2>"$WORK_DIR/build.err" || fail "build: $(cat "$WORK_DIR/build.txt" "$WORK_DIR/build.err")"
grep -q "added graph 'code'" "$WORK_DIR/build.txt" || fail "the entry was not recorded: $(cat "$WORK_DIR/build.txt")"
grep -q "Code graph for \"code\" (no model):" "$WORK_DIR/build.txt" || fail "no code summary: $(cat "$WORK_DIR/build.txt")"
grep -q "Files used:       4 (python 4) -- 4 parsed, 0 unchanged" "$WORK_DIR/build.txt" || fail "wrong file counts: $(cat "$WORK_DIR/build.txt")"
grep -q "unsupported language (.lua): 1" "$WORK_DIR/build.txt" || fail "the Lua file was not counted: $(cat "$WORK_DIR/build.txt")"
grep -q "src/tool.lua" "$WORK_DIR/build.txt" || fail "the Lua file was not named: $(cat "$WORK_DIR/build.txt")"
grep -q "Excluded:         third_party/ (1 file(s)" "$WORK_DIR/build.txt" || fail "the vendored directory was not left out: $(cat "$WORK_DIR/build.txt")"
grep -q "sources:" "$CONFIG" || fail "the config has no sources: entry"
[ -f "$APOGEE_HOME/embeddings/graphs/code.db" ] || fail "no graph database"

# --- stats: the origin split ----------------------------------------------------
"$APOGEE_BIN" graph stats code </dev/null >"$WORK_DIR/stats.txt" 2>&1 || fail "stats"
grep -q "extracted (parsed from source), 0 inferred (asserted by a model)" "$WORK_DIR/stats.txt" || fail "no origin split: $(cat "$WORK_DIR/stats.txt")"
grep -q "Code:      4 file(s) parsed (python 4)" "$WORK_DIR/stats.txt" || fail "no code line: $(cat "$WORK_DIR/stats.txt")"

# --- show: callers and callees across files, each at its line ---------------
"$APOGEE_BIN" graph show code pkg.models.User.greet </dev/null >"$WORK_DIR/show.txt" 2>&1 || fail "show"
grep -q "definition  src: pkg/models.py:10-11" "$WORK_DIR/show.txt" || fail "no definition line: $(cat "$WORK_DIR/show.txt")"
grep -q "<- pkg.service.run (function) -- extracted" "$WORK_DIR/show.txt" || fail "the cross-file caller is missing: $(cat "$WORK_DIR/show.txt")"
grep -q "at pkg/service.py:12" "$WORK_DIR/show.txt" || fail "the call site is missing: $(cat "$WORK_DIR/show.txt")"
grep -q "\-> pkg.models.Base.describe (function) -- extracted" "$WORK_DIR/show.txt" || fail "the inherited callee is missing: $(cat "$WORK_DIR/show.txt")"
"$APOGEE_BIN" graph show code os.path.basename </dev/null >"$WORK_DIR/name.txt" 2>&1 || fail "show a name"
grep -q "(name)" "$WORK_DIR/name.txt" || fail "an unresolved call is not a name node: $(cat "$WORK_DIR/name.txt")"

# --- update: one file edited, one parsed, the graph a fresh build's ----------
printf '\n\ndef farewell(name: str) -> str:\n    return "bye " + name\n' >> "$SRC_ROOT/src/pkg/models.py"
"$APOGEE_BIN" graph update code </dev/null >"$WORK_DIR/update.txt" 2>"$WORK_DIR/update.err" || fail "update: $(cat "$WORK_DIR/update.err")"
grep -q "1 parsed, 3 unchanged" "$WORK_DIR/update.txt" || fail "update parsed more than the edit: $(cat "$WORK_DIR/update.txt")"
grep -q "parsing src/pkg/models.py" "$WORK_DIR/update.err" || fail "the edited file was not parsed: $(cat "$WORK_DIR/update.err")"
if grep -q "parsing src/pkg/service.py" "$WORK_DIR/update.err"; then
    fail "an unchanged file was parsed: $(cat "$WORK_DIR/update.err")"
fi
"$APOGEE_BIN" graph build --source "$SRC_ROOT/src" --graph fresh -q </dev/null >/dev/null 2>&1 || fail "fresh build"
"$APOGEE_BIN" graph stats code </dev/null 2>&1 | sed 's/"code"/"GRAPH"/; s/update code`/update GRAPH`/' >"$WORK_DIR/stats.updated"
"$APOGEE_BIN" graph stats fresh </dev/null 2>&1 | sed 's/"fresh"/"GRAPH"/; s/update fresh`/update GRAPH`/' >"$WORK_DIR/stats.fresh"
cmp -s "$WORK_DIR/stats.updated" "$WORK_DIR/stats.fresh" || fail "the update did not converge on a fresh build: $(diff "$WORK_DIR/stats.updated" "$WORK_DIR/stats.fresh")"
"$APOGEE_BIN" graph show code pkg.models.farewell </dev/null >/dev/null 2>&1 || fail "the new function is not in the updated graph"

# The same bytes written again -- a touch -- parse nothing.
cp "$SRC_ROOT/src/pkg/models.py" "$WORK_DIR/models.copy"
cp "$WORK_DIR/models.copy" "$SRC_ROOT/src/pkg/models.py"
"$APOGEE_BIN" graph update code -q </dev/null >"$WORK_DIR/touch.txt" 2>&1 || fail "update after a touch"
grep -q "0 parsed, 4 unchanged" "$WORK_DIR/touch.txt" || fail "a touch re-parsed: $(cat "$WORK_DIR/touch.txt")"

# --- communities: clustered with no model, the summaries said absent ---------
"$APOGEE_BIN" graph communities code --min-size 2 -q </dev/null >"$WORK_DIR/communities.txt" 2>&1 || fail "communities: $(cat "$WORK_DIR/communities.txt")"
grep -q "no model; summaries absent (no generation backend is configured)" "$WORK_DIR/communities.txt" || fail "the absence was not said: $(cat "$WORK_DIR/communities.txt")"
grep -q "Summaries absent:" "$WORK_DIR/communities.txt" || fail "no absence count: $(cat "$WORK_DIR/communities.txt")"
"$APOGEE_BIN" graph communities code --list </dev/null >"$WORK_DIR/list.txt" 2>&1 || fail "communities --list"
grep -q "Community #1" "$WORK_DIR/list.txt" || fail "no community stored: $(cat "$WORK_DIR/list.txt")"

# --- dedupe: code merged by identity, the prose pass skipped and said --------
"$APOGEE_BIN" graph dedupe code </dev/null >"$WORK_DIR/dedupe.txt" 2>&1 || fail "dedupe: $(cat "$WORK_DIR/dedupe.txt")"
grep -q "Code entities: .* merged by exact qualified name" "$WORK_DIR/dedupe.txt" || fail "no code line: $(cat "$WORK_DIR/dedupe.txt")"
grep -q "Prose entities: vector dedupe skipped" "$WORK_DIR/dedupe.txt" || fail "the prose skip was not said: $(cat "$WORK_DIR/dedupe.txt")"

# --- expansion over a graph holding code beside a collection -----------------
# A mock answers (it echoes the system prompt, so the injected section shows)
# and a mock extracts nothing from the prose; the code is the graph.
cat > "$WORK_DIR/echo.json" <<'JSON'
{"turns": [{"text": "SYSTEM WAS: {{system}}"}]}
JSON
cat > "$WORK_DIR/nothing.json" <<'JSON'
{"turns": [{"text": "{\"entities\": [], \"relations\": []}"}]}
JSON
"$APOGEE_BIN" config add-backend echo --type mock --model-path "$WORK_DIR/echo.json" >/dev/null || fail "add-backend echo"
"$APOGEE_BIN" config add-backend nothing --type mock --model-path "$WORK_DIR/nothing.json" >/dev/null || fail "add-backend nothing"
"$APOGEE_BIN" config set-default echo >/dev/null || fail "set-default"
printf 'The service greets every user by name.\n' > "$WORK_DIR/docs/service.md"
"$APOGEE_BIN" embed ingest notes "$WORK_DIR/docs" </dev/null >/dev/null 2>&1 || fail "ingest"
"$APOGEE_BIN" config add-graph code --collections notes --sources "$SRC_ROOT/src" --extract-backend nothing --force </dev/null >/dev/null 2>&1 || fail "add-graph code with a collection"
"$APOGEE_BIN" graph build code -q </dev/null >"$WORK_DIR/mixed.txt" 2>&1 || fail "mixed build: $(cat "$WORK_DIR/mixed.txt")"
grep -q "0 parsed, 4 unchanged" "$WORK_DIR/mixed.txt" || fail "the mixed build re-parsed the code: $(cat "$WORK_DIR/mixed.txt")"
"$APOGEE_BIN" complete --rag notes "make_user" </dev/null >"$WORK_DIR/turn.out" 2>"$WORK_DIR/turn.err" || fail "complete --rag: $(cat "$WORK_DIR/turn.err")"
grep -q "\[Knowledge graph: code\]" "$WORK_DIR/turn.out" || fail "no graph section: $(cat "$WORK_DIR/turn.out")"
grep -q "pkg.service.make_user (function, pkg/service.py:6): def make_user(name: str) -> User" "$WORK_DIR/turn.out" || fail "the code entity was not injected with its line: $(cat "$WORK_DIR/turn.out")"
grep -q "·extracted\]→" "$WORK_DIR/turn.out" || fail "the parsed relation was not marked: $(cat "$WORK_DIR/turn.out")"
grep -q "graph entities" "$WORK_DIR/turn.err" || fail "the status line did not count the graph entities: $(cat "$WORK_DIR/turn.err")"

# --- delete ----------------------------------------------------------------------
"$APOGEE_BIN" graph delete code </dev/null >/dev/null 2>&1 || fail "delete"
[ ! -e "$APOGEE_HOME/embeddings/graphs/code.db" ] || fail "delete left the database"

echo "code_graph_e2e: ok"
