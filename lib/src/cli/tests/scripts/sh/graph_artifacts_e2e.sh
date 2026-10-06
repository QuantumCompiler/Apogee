#!/bin/sh
# Graph artifacts end to end on the real binary (27m). On a sandbox with ZERO
# backends, no key and the network pointed at a closed port: a code graph
# built from the Python fixture tree and clustered, both with no model; then
# `graph report` printed, with `--out` writing the same bytes and
# `--output-format json` one document; `graph export html` landing under its
# default name in the working directory -- its data block, its cap note, no
# external reference, and (where node is installed) its own script driven
# through layout, click-to-explain, search and the legend; `graph export
# graphml` read back by Python's stdlib XML parser and, when installed, by
# networkx's GraphML reader; `graph export mermaid` a call flow its grammar
# accepts. Throughout: a canary planted in the source's comments and in the
# private layout rows reaches no artifact, and the data directory is byte for
# byte what it was -- the artifacts land in the working directory alone.
#
# POSIX only, like the other shell checks. python3 and node are optional:
# without one, the checks it runs are skipped and said.
set -eu

APOGEE_BIN="${1:?usage: graph_artifacts_e2e.sh <apogee-binary> <work-dir> <fixtures-dir> <graph_html_check.js>}"
WORK_DIR="${2:?usage: graph_artifacts_e2e.sh <apogee-binary> <work-dir> <fixtures-dir> <graph_html_check.js>}"
FIXTURES="${3:?usage: graph_artifacts_e2e.sh <apogee-binary> <work-dir> <fixtures-dir> <graph_html_check.js>}"
HTML_CHECK="${4:?usage: graph_artifacts_e2e.sh <apogee-binary> <work-dir> <fixtures-dir> <graph_html_check.js>}"

rm -rf "$WORK_DIR"
mkdir -p "$WORK_DIR/home" "$WORK_DIR/here"
export APOGEE_HOME="$WORK_DIR/home"
unset ANTHROPIC_API_KEY OPENAI_API_KEY GEMINI_API_KEY GOOGLE_API_KEY
export http_proxy=http://127.0.0.1:9 https_proxy=http://127.0.0.1:9 \
    HTTP_PROXY=http://127.0.0.1:9 HTTPS_PROXY=http://127.0.0.1:9 ALL_PROXY=http://127.0.0.1:9
# The tree lives outside any repository, so it is read as it is on disk.
SRC_ROOT=$(mktemp -d "${TMPDIR:-/tmp}/apogee-graph-artifacts.XXXXXX")
trap 'rm -rf "$WORK_DIR" "$SRC_ROOT"' EXIT

fail() { echo "graph_artifacts_e2e: $*" >&2; exit 1; }
CANARY="PRIVATE-CANARY-27m-e2e-7d41"

cp -R "$FIXTURES/code_graph/python" "$SRC_ROOT/src"
# Source text a parse reads but no artifact may carry: a comment.
printf '\n# the deploy key is %s\n' "$CANARY" >>"$SRC_ROOT/src/main.py"
"$APOGEE_BIN" config init >/dev/null || fail "config init"
if "$APOGEE_BIN" config get backends </dev/null 2>/dev/null | grep -q "type:"; then
    fail "the sandbox has a backend configured"
fi
"$APOGEE_BIN" graph build --source "$SRC_ROOT/src" --graph code -q </dev/null >"$WORK_DIR/build.txt" 2>&1 \
    || fail "graph build --source: $(cat "$WORK_DIR/build.txt")"
"$APOGEE_BIN" graph communities code --no-summaries -q </dev/null >"$WORK_DIR/communities.txt" 2>&1 \
    || fail "graph communities: $(cat "$WORK_DIR/communities.txt")"

# The private layout rows, each holding the canary.
mkdir -p "$APOGEE_HOME/knowledge/raw" "$APOGEE_HOME/sessions" "$APOGEE_HOME/memory" \
    "$APOGEE_HOME/tasks/task-1" "$APOGEE_HOME/attachments"
printf 'raw conversation: %s\n' "$CANARY" >"$APOGEE_HOME/knowledge/raw/kr-0001.md"
printf '{"text":"%s"}\n' "$CANARY" >"$APOGEE_HOME/sessions/chat-1.jsonl"
printf '%s\n' "$CANARY" >"$APOGEE_HOME/memory/notes.txt"
printf '{"goal":"%s"}\n' "$CANARY" >"$APOGEE_HOME/tasks/task-1/task.json"
printf '%s\n' "$CANARY" >"$APOGEE_HOME/attachments/chat-1.txt"

# Every file under the data directory, with its checksum.
listing() { (cd "$APOGEE_HOME" && find . -type f -exec cksum {} + | sort); }
BEFORE=$(listing)

cd "$WORK_DIR/here"

# --- the report -------------------------------------------------------------------
"$APOGEE_BIN" graph report </dev/null >report.txt 2>report.err || fail "graph report: $(cat report.err)"
[ -s report.err ] && fail "graph report wrote to stderr: $(cat report.err)"
grep -q "^# Graph report: code$" report.txt || fail "the report's title: $(head -3 report.txt)"
grep -q "^23 relations: 23 extracted (parsed from source), 0 inferred (asserted by a model)\.$" report.txt \
    || fail "the origin mix: $(cat report.txt)"
grep -q "^1\. \`pkg.models.User\` (class, \`pkg/models.py:6\`) -- 6 relations (2 out, 4 in), 1 mention$" report.txt \
    || fail "the top hub: $(cat report.txt)"
grep -q "^_No summary -- clustered with no model\._$" report.txt || fail "the no-summaries fallback: $(cat report.txt)"
grep -q "^## Summary$" report.txt || fail "no summary section"
[ "$(grep '^## ' report.txt | tail -1)" = "## Summary" ] || fail "the summary is not last"
"$APOGEE_BIN" graph report --graph code --out report.md </dev/null >said.txt || fail "graph report --out"
cmp -s report.md report.txt || fail "--out wrote other bytes than the report printed"
grep -q "^Wrote the report on graph \"code\" to report.md " said.txt || fail "--out said: $(cat said.txt)"
"$APOGEE_BIN" graph report --output-format json </dev/null >report.json || fail "graph report json"
[ "$(wc -l <report.json | tr -d ' ')" = "1" ] || fail "the report's document is not one line"
grep -q '^{"communities":' report.json || fail "the report's document: $(head -c 200 report.json)"
grep -q '"object":"graph.report"' report.json || fail "the report's object"

# --- the HTML page, under its default name ----------------------------------------
"$APOGEE_BIN" graph export html </dev/null >html.txt 2>&1 || fail "graph export html: $(cat html.txt)"
[ -f code.html ] || fail "no code.html in the working directory: $(cat html.txt)"
grep -q "^Exported graph \"code\" to code.html: 12 entities and 18 relations drawn, " html.txt \
    || fail "the export said: $(cat html.txt)"
grep -q "^Showing all 12 entities; the 5 unresolved names are left out\.$" html.txt || fail "the cap note: $(cat html.txt)"
grep -q '<p id="cap-note">Showing all 12 entities; the 5 unresolved names are left out.</p>' code.html \
    || fail "the page does not carry its cap note"
grep -q '<script type="application/json" id="graph-data">{' code.html || fail "the page has no data block"
if grep -i -n -E 'https?:|//|src=|href=|url\(|@import|<link|<iframe|<img|fetch\(|xmlhttprequest|websocket|eventsource|sendbeacon' code.html \
    >refs.txt; then
    fail "the page holds an external reference: $(head -5 refs.txt)"
fi
"$APOGEE_BIN" graph export html --max-nodes 3 --out capped.html </dev/null >capped.txt || fail "a capped export"
grep -q "^Showing the 3 highest-degree of 12 entities -- capped at 3 by degree rank; the 5 unresolved names are left out\. apogee graph export graphml carries every one\.$" capped.txt \
    || fail "the capped note: $(cat capped.txt)"
grep -q "capped at 3 by degree rank" capped.html || fail "the capped page does not say so"
if command -v node >/dev/null 2>&1; then
    node "$HTML_CHECK" code.html make_user >node.txt 2>&1 || fail "the page's script: $(cat node.txt)"
    node "$HTML_CHECK" capped.html User >>node.txt 2>&1 || fail "the capped page's script: $(cat node.txt)"
else
    echo "graph_artifacts_e2e: node not found; the page's script is not driven"
fi

# --- GraphML, read back by parsers that are not ours --------------------------------
"$APOGEE_BIN" graph export graphml --graph code </dev/null >graphml.txt || fail "graph export graphml"
[ -f code.graphml ] || fail "no code.graphml: $(cat graphml.txt)"
grep -q "every entity (17) and relation (23), uncapped" graphml.txt || fail "the GraphML export said: $(cat graphml.txt)"
if command -v python3 >/dev/null 2>&1; then
    python3 - code.graphml <<'PY' || fail "GraphML did not read back"
import sys
import xml.etree.ElementTree as ET
ns = {"g": "http://graphml.graphdrawing.org/xmlns"}
root = ET.parse(sys.argv[1]).getroot()
graph = root.find("g:graph", ns)
nodes = graph.findall("g:node", ns)
edges = graph.findall("g:edge", ns)
assert len(nodes) == 17, len(nodes)
assert len(edges) == 23, len(edges)
ids = {n.get("id") for n in nodes}
assert all(e.get("source") in ids and e.get("target") in ids for e in edges)
keys = {k.get("id"): k.get("attr.name") for k in root.findall("g:key", ns)}
names = {d.text for n in nodes for d in n.findall("g:data", ns) if keys[d.get("key")] == "name"}
assert "pkg.service.make_user" in names and "json.dumps" in names, names
try:
    import networkx
except ImportError:
    print("graph_artifacts_e2e: networkx not installed; the stdlib parser alone read it")
else:
    g = networkx.read_graphml(sys.argv[1])
    assert g.number_of_nodes() == 17 and g.number_of_edges() == 23
    made = [d for _, d in g.nodes(data=True) if d.get("name") == "pkg.service.make_user"]
    assert made and made[0]["file"] == "pkg/service.py" and made[0]["line"] == 6, made
    print("graph_artifacts_e2e: networkx read every node and edge")
PY
else
    echo "graph_artifacts_e2e: python3 not found; GraphML is not read back"
fi

# --- Mermaid ---------------------------------------------------------------------------
"$APOGEE_BIN" graph export mermaid </dev/null >mermaid.txt || fail "graph export mermaid"
[ -f code.mmd ] || fail "no code.mmd: $(cat mermaid.txt)"
[ "$(head -1 code.mmd)" = "flowchart LR" ] || fail "not a flowchart: $(head -1 code.mmd)"
grep -q '^  subgraph f[0-9]*\["pkg/service.py"\]$' code.mmd || fail "no file group: $(cat code.mmd)"
grep -q '^    n[0-9]*\["make_user"\]$' code.mmd || fail "no make_user: $(cat code.mmd)"
grep -q -- '^  n[0-9]* --> n[0-9]*$' code.mmd || fail "no call drawn: $(cat code.mmd)"
# Every line is one the flowchart grammar reads.
if grep -v -E '^(flowchart LR|%% .*|  subgraph [A-Za-z_][A-Za-z0-9_]*\["[^"]*"\]|  end|    n[0-9]+\["[^"]*"\]|  n[0-9]+ (-->|-\.->) n[0-9]+)$' code.mmd >bad.txt; then
    fail "lines Mermaid would not read: $(cat bad.txt)"
fi

# --- selection, and a refusal --------------------------------------------------------
if "$APOGEE_BIN" graph report --graph nothing </dev/null >none.txt 2>&1; then
    fail "a graph that does not exist was reported"
fi
grep -q "apogee graph: no graph or collection named 'nothing'" none.txt || fail "the refusal: $(cat none.txt)"

# --- the privacy sweep, and the data directory untouched -------------------------------
for artifact in report.txt report.md report.json code.html capped.html code.graphml code.mmd; do
    if grep -q "$CANARY" "$artifact"; then
        fail "$artifact carries the canary"
    fi
done
[ "$(listing)" = "$BEFORE" ] || fail "the data directory changed: $(listing)"

echo "apogee graph artifacts end-to-end: OK"
