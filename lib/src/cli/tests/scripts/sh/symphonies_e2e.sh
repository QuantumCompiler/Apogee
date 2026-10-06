#!/usr/bin/env bash
# Symphonies (27q) on the real binary, against scripted mock members, with no
# input after the command is typed:
#
#   * the shipped starters seeded by `check --fix`, byte for byte the
#     repository's assets/symphonies/, and listed as shipped beside the
#     user's config entry and spec file;
#   * `play` under a suite: the stages in order, each member's one request
#     exactly its rendered template -- proven by members that echo the brief
#     they were sent, stage two's echo holding stage one's -- each stage said
#     on stderr, the output alone on stdout; `--output-format json` one
#     document; `--suite` moving the members; stdin as the input; a schema
#     stage's JSON threaded on, and one outside its schema stopping the walk
#     named, with nothing on stdout;
#   * `create` through the one editor -- a backend in a stage refused at
#     parse with the reason and the file untouched -- `edit` through
#     $EDITOR written back as the entry, a starter edited becoming an entry
#     that stands in for it, and `delete` the exact inverse;
#   * a hand-written entry naming a backend refused when the config loads;
#   * chains (27r): two shipped starters chained by `create --play`, played
#     with the first's output on the second's wire -- an extraction member
#     whose JSON answer carries the brief it was sent -- every line naming
#     its position; a hand-written loop failing the load with its path; spec
#     files nesting five deep refused at play start with nothing sent; and
#     `symphony_caps.stage_calls` stopping a chain mid-walk, named, with no
#     answer;
#   * completion for the verbs and the names.
#
# POSIX only, like the other .sh checks here (CLAUDE.md -> Platforms).

set -uo pipefail

APOGEE_BIN="${1:?usage: symphonies_e2e.sh <apogee-binary> <work-dir> <assets-dir>}"
WORK_DIR="${2:?usage: symphonies_e2e.sh <apogee-binary> <work-dir> <assets-dir>}"
ASSETS_DIR="${3:?usage: symphonies_e2e.sh <apogee-binary> <work-dir> <assets-dir>}"

rm -rf "$WORK_DIR"
mkdir -p "$WORK_DIR/scripts"
export APOGEE_HOME="$WORK_DIR/home"
export HOME="$WORK_DIR"
trap 'rm -rf "$WORK_DIR"' EXIT
cd "$WORK_DIR" || exit 1

fail() { echo "symphonies: $*" >&2; exit 1; }

script() {  # script <backend> <json turns>
    printf '{"turns": [%s]}\n' "$2" > "$WORK_DIR/scripts/$1.json"
}
script root '{"text": "ROOT<{{last_user}}>"}'
script helper '{"text": "HELPER<{{last_user}}>"}'
script other '{"text": "OTHER<{{last_user}}>"}'
script scribe '{"text": "{\"topic\": \"cats\", \"people\": [], \"places\": [\"mat\"], \"dates\": [], \"facts\": [\"a cat sat\"]}"}'
script liar '{"text": "{\"title\": \"no topic here\"}"}'
script carrier '{"text": "{\"topic\": {{last_user:json}}, \"people\": [], \"places\": [], \"dates\": [], \"facts\": []}"}'

mkdir -p "$APOGEE_HOME/config"
CONFIG="$APOGEE_HOME/config/config.yaml"
{
    echo "# PRESERVE-ME: a comment the editor keeps"
    echo "backends:"
    for name in root helper other scribe liar carrier; do
        echo "  $name:"
        echo "    type: mock"
        echo "    model_path: $WORK_DIR/scripts/$name.json"
    done
    echo "models:"
    echo "  default: root"
    echo "  default_suite: duo"
    echo "memory:"
    echo "  recall: false"
    echo "suites:"
    echo "  duo:"
    echo "    members:"
    echo "      chat: root"
    echo "      utility: helper"
    echo "      extraction: scribe"
    echo "  swapped:"
    echo "    members:"
    echo "      chat: other"
    echo "      utility: other"
    echo "      extraction: liar"
    echo "  carried:"
    echo "    members:"
    echo "      chat: root"
    echo "      utility: helper"
    echo "      extraction: carrier"
} > "$CONFIG"
"$APOGEE_BIN" check --fix </dev/null >"$WORK_DIR/fix.txt" 2>&1 || fail "check --fix: $(cat "$WORK_DIR/fix.txt")"

# --- The starters, seeded byte for byte --------------------------------------
for starter in "$ASSETS_DIR"/*.yaml; do
    name="$(basename "$starter")"
    cmp -s "$starter" "$APOGEE_HOME/symphonies/$name" ||
        fail "the seeded $name is not the shipped asset"
done
[ "$(ls "$APOGEE_HOME/symphonies" | wc -l | tr -d ' ')" = "$(ls "$ASSETS_DIR" | wc -l | tr -d ' ')" ] ||
    fail "seeding wrote a different set of starters than ships"

# --- list and show -------------------------------------------------------------
printf 'stages:\n  - {name: echo, role: chat, prompt: "Echo {{input}}"}\n' \
    > "$APOGEE_HOME/symphonies/mine.yaml"
LIST=$("$APOGEE_BIN" symphonies list </dev/null 2>&1) || fail "list: $LIST"
echo "$LIST" | grep -Eq '^summarize-verify +utility → chat +shipped ' || fail "list: no shipped starter: $LIST"
echo "$LIST" | grep -Eq '^mine +chat +file ' || fail "list: the spec file is not listed as one: $LIST"
SHOW=$("$APOGEE_BIN" symphonies show extract-facts </dev/null 2>&1) || fail "show: $SHOW"
echo "$SHOW" | grep -q 'answer held to its schema:' || fail "show: no schema contract: $SHOW"
echo "$SHOW" | grep -q 'input: The text to read.' || fail "show: no input contract: $SHOW"

# --- play: stages in order, the wire exactly each rendered template ------------
"$APOGEE_BIN" symphonies play summarize-verify --input "The cat sat on the mat on 4 May." \
    </dev/null >"$WORK_DIR/out.txt" 2>"$WORK_DIR/err.txt" || fail "play: $(cat "$WORK_DIR/err.txt")"
EXPECTED="ROOT<Here is a passage and a summary of it.

Passage:
The cat sat on the mat on 4 May.

Summary:
HELPER<Summarize the passage below in at most three sentences. Keep every name, number and date exactly as the passage gives it.

Passage:
The cat sat on the mat on 4 May.
>

Check the summary against the passage. Correct anything it gets wrong and add any key fact it leaves out, in at most three sentences. Reply with the final summary only.
>"
[ "$(cat "$WORK_DIR/out.txt")" = "$EXPECTED" ] ||
    fail "play: the output is not stage two's echo of its exact brief:
$(cat "$WORK_DIR/out.txt")"
grep -q '^stage 1/2 summarize — asking utility (helper): ' "$WORK_DIR/err.txt" ||
    fail "play: stage 1 not said: $(cat "$WORK_DIR/err.txt")"
grep -q '^stage 2/2 verify — asking chat (root): ' "$WORK_DIR/err.txt" ||
    fail "play: stage 2 not said: $(cat "$WORK_DIR/err.txt")"
[ "$(wc -l < "$WORK_DIR/err.txt" | tr -d ' ')" = "2" ] || fail "play: stderr holds more than the two stages"

# One document, nothing else on stdout; the suite named.
JSON=$("$APOGEE_BIN" symphonies play summarize-verify --input "x" --output-format json </dev/null 2>/dev/null) ||
    fail "play json"
[ "$(echo "$JSON" | wc -l | tr -d ' ')" = "1" ] || fail "play json: more than one line"
echo "$JSON" | python3 -c '
import json, sys
d = json.load(sys.stdin)
assert d["object"] == "symphony.play" and d["suite"] == "duo", d
assert [s["backend"] for s in d["stages"]] == ["helper", "root"], d
assert d["output"] == d["stages"][1]["answer"], d
' || fail "play json: $JSON"

# --suite moves the members; stdin is the input.
OUT=$(echo "piped passage" | "$APOGEE_BIN" symphonies play summarize-verify --suite swapped -q 2>&1) ||
    fail "play --suite: $OUT"
echo "$OUT" | grep -q '^OTHER<Here is a passage' || fail "play --suite: not the swapped members: $OUT"
echo "$OUT" | grep -q '^piped passage$' || fail "play: stdin was not the input: $OUT"

# A schema stage: valid JSON threaded out; one outside its schema stopped, named.
OUT=$("$APOGEE_BIN" symphonies play extract-facts --input "A cat sat on the mat." -q </dev/null 2>&1) ||
    fail "play extract-facts: $OUT"
echo "$OUT" | python3 -c 'import json,sys; assert json.load(sys.stdin)["topic"] == "cats"' ||
    fail "play extract-facts: not the member's JSON: $OUT"
"$APOGEE_BIN" symphonies play extract-facts --suite swapped --input "x" </dev/null \
    >"$WORK_DIR/out.txt" 2>"$WORK_DIR/err.txt"
[ $? -eq 2 ] || fail "a schema miss is not a member failure (exit 2)"
[ -s "$WORK_DIR/out.txt" ] && fail "a stopped play printed an output: $(cat "$WORK_DIR/out.txt")"
grep -q "stage 1/1 extract (extraction): 'liar' answered outside the stage's schema" "$WORK_DIR/err.txt" ||
    fail "the schema miss is not named: $(cat "$WORK_DIR/err.txt")"

# A refusal before anything runs: no input given.
"$APOGEE_BIN" symphonies play summarize-verify </dev/null >/dev/null 2>"$WORK_DIR/err.txt"
[ $? -eq 1 ] || fail "a play with no input is not refused"
grep -q "and none was given" "$WORK_DIR/err.txt" || fail "no-input refusal: $(cat "$WORK_DIR/err.txt")"

# --- create, edit, delete through the one editor -------------------------------
BEFORE=$(cat "$CONFIG")
"$APOGEE_BIN" symphonies create bad --stage 'one:helper:Do {{input}}' </dev/null >/dev/null 2>"$WORK_DIR/err.txt" &&
    fail "a stage naming a backend was accepted"
grep -q "'helper' is a backend -- a stage names the role it plays" "$WORK_DIR/err.txt" ||
    fail "the backend refusal names no reason: $(cat "$WORK_DIR/err.txt")"
[ "$(cat "$CONFIG")" = "$BEFORE" ] || fail "a refused create touched the config"

"$APOGEE_BIN" symphonies create pair --description "Two steps." \
    --stage 'first:utility:Do: {{input}}' --stage 'second:chat:Check {{first}}' \
    </dev/null >/dev/null 2>&1 || fail "create pair"
grep -q '^# PRESERVE-ME' "$CONFIG" || fail "create dropped a comment"
OUT=$("$APOGEE_BIN" symphonies play pair --input "go" -q </dev/null 2>&1) || fail "play pair: $OUT"
[ "$OUT" = "ROOT<Check HELPER<Do: go>>" ] || fail "play pair: $OUT"

# edit: $EDITOR changes the description; the entry is written back.
cat > "$WORK_DIR/editor.sh" <<'EOF'
#!/bin/sh
sed 's/^description: .*/description: Edited./' "$1" > "$1.new" && mv "$1.new" "$1"
EOF
chmod +x "$WORK_DIR/editor.sh"
EDITOR="$WORK_DIR/editor.sh" "$APOGEE_BIN" symphonies edit pair </dev/null >/dev/null 2>"$WORK_DIR/err.txt" ||
    fail "edit pair: $(cat "$WORK_DIR/err.txt")"
grep -q '^    description: Edited\.$' "$CONFIG" || fail "edit was not written back: $(cat "$CONFIG")"
# A starter edited becomes an entry standing in for it; delete brings it back.
EDITOR="$WORK_DIR/editor.sh" "$APOGEE_BIN" symphonies edit summarize-verify </dev/null >"$WORK_DIR/out.txt" 2>&1 ||
    fail "edit starter: $(cat "$WORK_DIR/out.txt")"
"$APOGEE_BIN" symphonies list </dev/null 2>/dev/null | grep -Eq '^summarize-verify +utility → chat +config \(overrides shipped\) +Edited\.' ||
    fail "the edited starter is not an entry standing in for it"
"$APOGEE_BIN" symphonies delete summarize-verify </dev/null >/dev/null 2>&1 || fail "delete the override"
"$APOGEE_BIN" symphonies delete pair </dev/null >/dev/null 2>&1 || fail "delete pair"
[ "$(cat "$CONFIG")" = "$BEFORE

symphonies:" ] || fail "delete is not the inverse of create:
$(cat "$CONFIG")"
"$APOGEE_BIN" symphonies delete summarize-verify </dev/null >/dev/null 2>"$WORK_DIR/err.txt" &&
    fail "a starter was deleted"
grep -q "is a shipped starter" "$WORK_DIR/err.txt" || fail "starter delete: $(cat "$WORK_DIR/err.txt")"

# --- A hand-written entry naming a backend fails the load, with the reason -----
cp "$CONFIG" "$WORK_DIR/config.bak"
printf '  hand:\n    stages:\n      - {name: a, role: root, prompt: "{{input}}"}\n' >> "$CONFIG"
"$APOGEE_BIN" symphonies list </dev/null >/dev/null 2>"$WORK_DIR/err.txt" &&
    fail "a stage naming a backend loaded"
grep -q "symphonies.hand.stages\[0\].role: 'root' is a backend" "$WORK_DIR/err.txt" ||
    fail "the load refusal: $(cat "$WORK_DIR/err.txt")"
cp "$WORK_DIR/config.bak" "$CONFIG"

# --- Chains (27r) --------------------------------------------------------------
"$APOGEE_BIN" symphonies create digest --description "Summarize, then pull the facts." \
    --play summary:summarize-verify --play facts:extract-facts </dev/null >"$WORK_DIR/out.txt" 2>&1 ||
    fail "create the chain: $(cat "$WORK_DIR/out.txt")"
grep -q "(play:summarize-verify → play:extract-facts)" "$WORK_DIR/out.txt" ||
    fail "create did not say the chain: $(cat "$WORK_DIR/out.txt")"
"$APOGEE_BIN" symphonies play digest --suite carried --input "The cat sat on the mat on 4 May." \
    </dev/null >"$WORK_DIR/out.txt" 2>"$WORK_DIR/err.txt" || fail "play the chain: $(cat "$WORK_DIR/err.txt")"
# The extraction member's answer carries its brief: extract-facts' prompt
# rendered with summarize-verify's output -- the chat member's echo -- as its
# input, and the chain's own input only inside that.
python3 - "$WORK_DIR/out.txt" <<'EOF' || fail "the chain's output is not the second starter's answer to the first's output: $(cat "$WORK_DIR/out.txt")"
import json, sys
topic = json.load(open(sys.argv[1]))["topic"]
assert topic.startswith("Read the text below and record its key facts"), topic
assert "\n\nText:\nROOT<Here is a passage and a summary of it." in topic, topic
assert "Summary:\nHELPER<Summarize the passage below" in topic, topic
EOF
for line in \
    '^digest → summarize-verify, stage 1/2 summarize — asking utility (helper): ' \
    '^digest → summarize-verify, stage 2/2 verify — asking chat (root): ' \
    '^digest → extract-facts, stage 1/1 extract — asking extraction (carrier): Read the text below'; do
    grep -q "$line" "$WORK_DIR/err.txt" || fail "the chain's narration has no '$line': $(cat "$WORK_DIR/err.txt")"
done
[ "$(wc -l < "$WORK_DIR/err.txt" | tr -d ' ')" = "3" ] || fail "the chain said more than its three calls"
JSON=$("$APOGEE_BIN" symphonies play digest --suite carried --input "x" --output-format json </dev/null 2>/dev/null) ||
    fail "play the chain as json"
echo "$JSON" | python3 -c '
import json, sys
d = json.load(sys.stdin)
assert [s["play"] for s in d["stages"]] == ["summarize-verify", "extract-facts"], d
assert [s["backend"] for s in d["stages"][0]["stages"]] == ["helper", "root"], d
assert d["stages"][1]["stages"][0]["backend"] == "carrier", d
assert d["output"] == d["stages"][1]["answer"] == d["stages"][1]["stages"][0]["answer"], d
' || fail "the chain's document: $JSON"

# A loop written by hand fails the load, naming its path.
cp "$CONFIG" "$WORK_DIR/config.bak"
printf '  ping:\n    stages:\n      - {name: a, play: pong}\n  pong:\n    stages:\n      - {name: b, play: ping}\n' >> "$CONFIG"
"$APOGEE_BIN" symphonies list </dev/null >/dev/null 2>"$WORK_DIR/err.txt" && fail "a loop loaded"
grep -q "symphonies.ping: ping → pong, stage 1 (b): plays 'ping', which is already playing -- a loop, ping → pong → ping" \
    "$WORK_DIR/err.txt" || fail "the loop refusal: $(cat "$WORK_DIR/err.txt")"
cp "$WORK_DIR/config.bak" "$CONFIG"

# Spec files nesting five deep -- late-bound, so no load sees them -- are
# refused at play start, before any member is asked anything.
for level in 2 3 4; do
    printf 'stages:\n  - {name: down, play: n%s}\n' "$((level + 1))" > "$APOGEE_HOME/symphonies/n$level.yaml"
done
printf 'stages:\n  - {name: leaf, role: chat, prompt: "Leaf {{input}}"}\n' > "$APOGEE_HOME/symphonies/n5.yaml"
OUT=$("$APOGEE_BIN" symphonies play n2 --input go -q </dev/null 2>&1) || fail "four deep did not play: $OUT"
[ "$OUT" = "ROOT<Leaf go>" ] || fail "four deep: $OUT"
printf 'stages:\n  - {name: down, play: n2}\n' > "$APOGEE_HOME/symphonies/n1.yaml"
"$APOGEE_BIN" symphonies play n1 --input go </dev/null >"$WORK_DIR/out.txt" 2>"$WORK_DIR/err.txt"
[ $? -eq 1 ] || fail "five deep was not refused"
grep -q "plays 'n5', which nests 5 deep -- n1 → n2 → n3 → n4 → n5, and the cap is 4" "$WORK_DIR/err.txt" ||
    fail "the depth refusal: $(cat "$WORK_DIR/err.txt")"
grep -q "asking" "$WORK_DIR/err.txt" && fail "a member was asked before the depth refusal"
rm -f "$APOGEE_HOME"/symphonies/n[1-5].yaml

# One budget for the whole walk: two plays of a two-stage starter under a cap
# of three stop at the fourth call, named, with nothing on stdout.
printf 'symphony_caps:\n  stage_calls: 3\n' >> "$CONFIG"
"$APOGEE_BIN" symphonies create twice --play a:summarize-verify --play b:summarize-verify \
    </dev/null >/dev/null 2>&1 || fail "create twice"
"$APOGEE_BIN" symphonies play twice --input P </dev/null >"$WORK_DIR/out.txt" 2>"$WORK_DIR/err.txt"
[ $? -eq 1 ] || fail "a spent budget did not exit 1"
[ -s "$WORK_DIR/out.txt" ] && fail "a stopped chain printed an output: $(cat "$WORK_DIR/out.txt")"
grep -q "twice → summarize-verify, stage 2/2 verify (chat): the play's budget is spent -- 3 of 3 member calls made" \
    "$WORK_DIR/err.txt" || fail "the budget stop: $(cat "$WORK_DIR/err.txt")"
[ "$(grep -c "asking" "$WORK_DIR/err.txt")" = "3" ] || fail "the budget did not hold the walk to three calls"
"$APOGEE_BIN" symphonies delete twice </dev/null >/dev/null 2>&1 || fail "delete twice"
"$APOGEE_BIN" symphonies delete digest </dev/null >/dev/null 2>&1 || fail "delete digest"
cp "$WORK_DIR/config.bak" "$CONFIG"

# --- Completion: the verbs and the names --------------------------------------
OFFERED=$("$APOGEE_BIN" __complete symphonies "" </dev/null 2>/dev/null)
for verb in list show create edit delete play; do
    echo "$OFFERED" | grep -qx "$verb" || fail "completion: verb $verb not offered: $OFFERED"
done
OFFERED=$("$APOGEE_BIN" __complete symphonies play "" </dev/null 2>/dev/null)
for name in summarize-verify extract-facts describe-answer mine; do
    echo "$OFFERED" | grep -qx "$name" || fail "completion: $name not offered: $OFFERED"
done

echo "symphonies: OK"
