#!/bin/sh
# Graph navigation end to end on the real binary (27l). On a sandbox with
# ZERO backends, no key and the network pointed at a closed port: a code
# graph built from the Python fixture tree with no model; `graph path` with
# each hop's relation and origin, and "no path within 8 hops" where there is
# none; `graph explain`, `graph neighbors` and `graph query`; a refusal that
# names the near matches. Then an external MCP client over stdio sees the
# four graph tools from `apogee __mcp-tools`, each marked read-only, and gets
# back exactly the documents the CLI printed with `--output-format json`.
# Then a tool-using turn on a scripted mock calls `graph_path` on a pipe --
# where nobody can answer the permission gate, so a gated tool would be
# denied -- and gets the route. Throughout, the graph's rows are unchanged:
# navigation never writes.
#
# POSIX only, like the other shell checks. Needs python3 for the MCP client
# (skips itself without one).
set -eu

APOGEE_BIN="${1:?usage: graph_navigation_e2e.sh <apogee-binary> <work-dir> <fixtures-dir> <mcp-client.py>}"
WORK_DIR="${2:?usage: graph_navigation_e2e.sh <apogee-binary> <work-dir> <fixtures-dir> <mcp-client.py>}"
FIXTURES="${3:?usage: graph_navigation_e2e.sh <apogee-binary> <work-dir> <fixtures-dir> <mcp-client.py>}"
CLIENT="${4:?usage: graph_navigation_e2e.sh <apogee-binary> <work-dir> <fixtures-dir> <mcp-client.py>}"

if ! command -v python3 >/dev/null 2>&1; then
    echo "graph_navigation_e2e: python3 not found; skipping"
    exit 0
fi

rm -rf "$WORK_DIR"
mkdir -p "$WORK_DIR/home" "$WORK_DIR/out"
export APOGEE_HOME="$WORK_DIR/home"
unset ANTHROPIC_API_KEY OPENAI_API_KEY GEMINI_API_KEY GOOGLE_API_KEY
export http_proxy=http://127.0.0.1:9 https_proxy=http://127.0.0.1:9 \
    HTTP_PROXY=http://127.0.0.1:9 HTTPS_PROXY=http://127.0.0.1:9 ALL_PROXY=http://127.0.0.1:9
# The tree lives outside any repository, so it is read as it is on disk.
SRC_ROOT=$(mktemp -d "${TMPDIR:-/tmp}/apogee-graph-nav.XXXXXX")
trap 'rm -rf "$WORK_DIR" "$SRC_ROOT"' EXIT

fail() { echo "graph_navigation_e2e: $*" >&2; exit 1; }
OUT="$WORK_DIR/out"

cp -R "$FIXTURES/code_graph/python" "$SRC_ROOT/src"
"$APOGEE_BIN" config init >/dev/null || fail "config init"
if "$APOGEE_BIN" config get backends </dev/null 2>/dev/null | grep -q "type:"; then
    fail "the sandbox has a backend configured"
fi
"$APOGEE_BIN" graph build --source "$SRC_ROOT/src" --graph code -q </dev/null >"$OUT/build.txt" 2>&1 \
    || fail "graph build --source: $(cat "$OUT/build.txt")"

# The graph's rows, as one digest -- what "navigation never writes" is held to.
DB="$APOGEE_HOME/embeddings/graphs/code.db"
rows() {
    python3 - "$DB" <<'PY'
import hashlib, sqlite3, sys
db = sqlite3.connect(sys.argv[1])
digest = hashlib.sha256()
for table in ("kg_nodes", "kg_edges", "kg_mentions", "kg_code_mentions", "kg_edge_sites",
              "kg_state", "kg_communities", "kg_community_members", "graph_meta"):
    for row in db.execute("SELECT * FROM %s ORDER BY 1, 2" % table):
        digest.update(repr(row).encode())
print(digest.hexdigest())
PY
}
BEFORE=$(rows)

# --- path: each hop with its relation and origin ------------------------------
"$APOGEE_BIN" graph path main.main describe --directed --relation calls </dev/null >"$OUT/path.txt" 2>&1 \
    || fail "graph path: $(cat "$OUT/path.txt")"
grep -q "('describe' is pkg.models.Base.describe, by its unqualified name)" "$OUT/path.txt" \
    || fail "the unqualified name was not said: $(cat "$OUT/path.txt")"
grep -q -- "-- 3 hop(s), directed, relations: calls:" "$OUT/path.txt" || fail "not three hops: $(cat "$OUT/path.txt")"
grep -q "  main.main -\[calls·extracted\]-> pkg.service.run  at main.py:" "$OUT/path.txt" \
    || fail "the first hop lacks its relation, origin or site: $(cat "$OUT/path.txt")"
grep -q "  pkg.models.User.greet -\[calls·extracted\]-> pkg.models.Base.describe  at pkg/models.py:" "$OUT/path.txt" \
    || fail "the last hop is wrong: $(cat "$OUT/path.txt")"
"$APOGEE_BIN" graph path describe main.main --directed </dev/null >"$OUT/nopath.txt" 2>&1 \
    || fail "graph path with no path: $(cat "$OUT/nopath.txt")"
grep -q "^No path within 8 hops between pkg.models.Base.describe" "$OUT/nopath.txt" \
    || fail "a missing connection was not said: $(cat "$OUT/nopath.txt")"

# --- explain, neighbors, query --------------------------------------------------
"$APOGEE_BIN" graph explain run </dev/null >"$OUT/explain.txt" 2>&1 || fail "graph explain: $(cat "$OUT/explain.txt")"
grep -q "^pkg.service.run (function, pkg/service.py:10) in graph \"code\"" "$OUT/explain.txt" \
    || fail "explain's heading: $(cat "$OUT/explain.txt")"
grep -q "  definition  src: pkg/service.py:10-12" "$OUT/explain.txt" || fail "explain's provenance: $(cat "$OUT/explain.txt")"
grep -q "  calls -> (" "$OUT/explain.txt" || fail "explain's relations: $(cat "$OUT/explain.txt")"
"$APOGEE_BIN" graph neighbors pkg.models.User --relation calls --direction in </dev/null >"$OUT/neighbors.txt" 2>&1 \
    || fail "graph neighbors: $(cat "$OUT/neighbors.txt")"
grep -q "pkg.service.make_user (function, pkg/service.py:6)  at pkg/service.py:" "$OUT/neighbors.txt" \
    || fail "neighbors: $(cat "$OUT/neighbors.txt")"
"$APOGEE_BIN" graph query make_user </dev/null >"$OUT/query.txt" 2>&1 || fail "graph query: $(cat "$OUT/query.txt")"
grep -q "^\[Knowledge graph: code\]$" "$OUT/query.txt" || fail "query's section: $(cat "$OUT/query.txt")"
grep -q "^pkg.service.make_user (function, pkg/service.py:6): def make_user(name: str) -> User$" "$OUT/query.txt" \
    || fail "query's entity: $(cat "$OUT/query.txt")"
if "$APOGEE_BIN" graph explain greeter </dev/null >"$OUT/none.txt" 2>&1; then
    fail "an unknown node was explained"
fi
grep -q "apogee graph: no node named 'greeter' in graph 'code'" "$OUT/none.txt" || fail "the refusal: $(cat "$OUT/none.txt")"

# --- the JSON documents, and an external MCP client ------------------------------
"$APOGEE_BIN" graph path main.main describe --directed --relation calls --output-format json </dev/null \
    >"$OUT/path.json" 2>"$OUT/path.err" || fail "path json: $(cat "$OUT/path.err")"
[ -s "$OUT/path.err" ] && fail "a read wrote to stderr: $(cat "$OUT/path.err")"
"$APOGEE_BIN" graph explain run --output-format json </dev/null >"$OUT/explain.json" || fail "explain json"
"$APOGEE_BIN" graph neighbors pkg.models.User --relation calls --direction in --output-format json </dev/null \
    >"$OUT/neighbors.json" || fail "neighbors json"
"$APOGEE_BIN" graph query make_user --output-format json </dev/null >"$OUT/query.json" || fail "query json"
for document in path explain neighbors query; do
    [ "$(wc -l <"$OUT/$document.json" | tr -d ' ')" = "1" ] || fail "$document is not one line"
    python3 -c 'import json,sys; json.load(open(sys.argv[1]))' "$OUT/$document.json" \
        || fail "$document is not JSON"
done
cat >"$OUT/calls.json" <<JSON
[{"tool": "graph_path", "expect": "$OUT/path.json",
  "arguments": {"from": "main.main", "to": "describe", "directed": true, "relations": ["calls"]}},
 {"tool": "graph_explain", "expect": "$OUT/explain.json", "arguments": {"node": "run"}},
 {"tool": "graph_neighbors", "expect": "$OUT/neighbors.json",
  "arguments": {"node": "pkg.models.User", "relation": "calls", "direction": "in"}},
 {"tool": "graph_query", "expect": "$OUT/query.json", "arguments": {"question": "make_user"}}]
JSON
python3 "$CLIENT" "$APOGEE_BIN" "$OUT/calls.json" >"$OUT/mcp.txt" 2>&1 \
    || fail "the MCP client: $(cat "$OUT/mcp.txt")"
grep -q "graph_path: the CLI's document, byte for byte" "$OUT/mcp.txt" || fail "graph_path not compared"

# --- a tool-using turn calls graph_path, unprompted by the gate ---------------------
cat >"$OUT/navigator.json" <<'JSON'
{"turns": [{"text": "", "tool_calls": [{"name": "graph_path", "arguments": {"from": "main.main", "to": "describe", "directed": true, "relations": ["calls"]}}]},
           {"text": "ROUTE {{last_tool_result}}"}]}
JSON
"$APOGEE_BIN" config add-backend navigator --type mock --model-path "$OUT/navigator.json" >/dev/null \
    || fail "add-backend navigator"
"$APOGEE_BIN" config set-default navigator >/dev/null || fail "set-default"
"$APOGEE_BIN" complete --tools "how does main reach describe?" </dev/null >"$OUT/turn.out" 2>"$OUT/turn.err" \
    || fail "the tool-using turn: $(cat "$OUT/turn.err")"
grep -q '^ROUTE {.*"found":true' "$OUT/turn.out" || fail "the turn did not get the route: $(cat "$OUT/turn.out")"
grep -q '"name":"pkg.models.Base.describe"' "$OUT/turn.out" || fail "the route lacks its end: $(cat "$OUT/turn.out")"
if grep -qi "denied\|permission" "$OUT/turn.err"; then
    fail "the gate was consulted: $(cat "$OUT/turn.err")"
fi

[ "$(rows)" = "$BEFORE" ] || fail "navigation changed the graph's rows"

echo "apogee graph navigation end-to-end: OK"
