#!/usr/bin/env bash
# The task runner (27h) on the real binary, against scripted mocks, with no
# input after the command is typed:
#
#   * the full cycle -- goal, plan, an incomplete round, the corrective round
#     naming what failed, done, exit 0 -- and the ledger's transition
#     sequence, one transition at a time;
#   * `task status` (the plan, the rounds used, each check's state, the
#     conversation) and `task list` (each task's outcome);
#   * an `ask`-level tool denied with nobody present, the denial the tool's
#     result and the turn going on, recorded in the ledger -- and nothing
#     prompting: stdin is a pipe held open, so a prompt would hang;
#   * the budget and the breaker each ending with an honest status that names
#     the unpassed checks, never a claimed success;
#   * the lock refusing a second `task run` while one is live, naming it;
#   * a task killed mid-round resumed from the ledger to done, the
#     conversation one session with no turn said twice;
#   * `task cancel` ending a running task's turn through the loop's
#     cancellation, and `task halt` stopping one when its round ends;
#   * `chats delete` refused on a live task's conversation, allowed once the
#     task is cancelled; and the task ids offered by completion;
#   * the autonomy policy (27i): a write granted with `--allow` run with
#     nothing prompting, the grant and its use with its target in the ledger
#     and `task status`; a question answered by `--on-question answer:` to
#     done, the question and the answer recorded -- and without it, the task
#     failing naming the question; a grant wider than the config or the
#     agent refused at `task run` naming the rule, with no task made; and the
#     repeatable flags taken whole;
#   * the surfaces (27j): a task run under `--output-format stream-json`
#     emitting its lifecycle around the turns' own events -- every stdout line
#     JSON, the `task_started` history and one event per transition after it
#     equal to the ledger's transition sequence, one for one, the grant's use
#     a `task_grant`, and the task's state rebuilt by a driver from the stream
#     alone equal to `task status --output-format json`, which `task_finished`
#     carries; `task list --output-format json` naming the same tasks as the
#     human list; and a halted task resumed in machine mode opening with
#     `task_started`, `resumed: true` and its whole history.
#
# Recall is off in the config so no summary spends a scripted turn. POSIX
# only, like the other .sh checks here (CLAUDE.md -> Platforms).

set -uo pipefail

APOGEE_BIN="${1:?usage: task_e2e.sh <apogee-binary> <work-dir>}"
WORK_DIR="${2:?usage: task_e2e.sh <apogee-binary> <work-dir>}"

rm -rf "$WORK_DIR"
mkdir -p "$WORK_DIR/scripts" "$WORK_DIR/project"
export APOGEE_HOME="$WORK_DIR/home"
export HOME="$WORK_DIR"
trap 'rm -rf "$WORK_DIR"' EXIT
cd "$WORK_DIR/project" || exit 1

fail() { echo "task: $*" >&2; exit 1; }

script() {  # script <backend> <json turns>
    printf '{"turns": [%s]}\n' "$2" > "$WORK_DIR/scripts/$1.json"
}

for name in cycle denied granted asked machine stalled killed cancelled halted; do
    script "$name" '{"text": "placeholder"}'
done
mkdir -p "$APOGEE_HOME/config"
{
    echo "backends:"
    for name in cycle denied granted asked machine stalled killed cancelled halted; do
        echo "  $name:"
        echo "    type: mock"
        echo "    model_path: $WORK_DIR/scripts/$name.json"
    done
    echo "models:"
    echo "  default: cycle"
    echo "memory:"
    echo "  recall: false"
} > "$APOGEE_HOME/config/config.yaml"
# A YAML config written by hand, named as the file to read (28i): the compat
# read under test, and no notice -- the user named the file.
export APOGEE_CONFIG="$APOGEE_HOME/config/config.yaml"
"$APOGEE_BIN" check --fix </dev/null >"$WORK_DIR/fix.txt" 2>&1 || fail "check --fix: $(cat "$WORK_DIR/fix.txt")"

# The newest task's id, and a field of a task's ledger.
newest() { "$APOGEE_BIN" task list </dev/null 2>/dev/null | head -n 1 | cut -d' ' -f1; }
ledger() { echo "$APOGEE_HOME/tasks/$1/task.json"; }
field() {  # field <task> <python expression over t>
    python3 -c "import json,sys; t=json.load(open(sys.argv[1])); print($2)" "$(ledger "$1")"
}
wait_for() {  # wait_for <task-or-glob> <grep pattern>: up to 20 s
    local i
    for i in $(seq 1 200); do
        grep -q "$2" $(ledger "$1") 2>/dev/null && return 0
        sleep 0.1
    done
    return 1
}
session_of() { echo "$APOGEE_HOME/sessions/$(field "$1" "t['session_id']").json"; }
# What a driver checks of a machine-mode task stream (27j): every line JSON,
# `session` first, the history `task_started` carries then one event per
# transition equal to the ledger's sequence from <first> on, and the state a
# driver rebuilds from the events alone equal to the status document.
#   stream_check <stream.jsonl> <ledger> <status.json> <resumed: 0|1>
stream_check() {
    python3 - "$@" <<'EOF'
import json, sys
lines = open(sys.argv[1]).read().splitlines()
events = [json.loads(line) for line in lines]   # every line on stdout is JSON
ledger = json.load(open(sys.argv[2]))
status = json.load(open(sys.argv[3]))
resumed = sys.argv[4] == "1"
assert events and events[0]["type"] == "session", events[:1]
lifecycle = [e for e in events if e["type"].startswith("task_") and e["type"] != "task_grant"]
assert lifecycle[0]["type"] == "task_started", lifecycle[0]
assert lifecycle[0]["resumed"] is resumed, lifecycle[0]
assert all(e["task_id"] == ledger["id"] for e in lifecycle)
seen = lifecycle[0]["history"] + [e["transition"] for e in lifecycle]
assert seen == ledger["transitions"], (seen, ledger["transitions"])
assert lifecycle[-1]["type"] == "task_finished" and lifecycle[-1]["task"] == status
# A driver's reconstruction, from task_started's view and the events alone.
state = None
for e in events:
    if e["type"] == "task_started":
        state = e["task"]
    if state is None or e not in lifecycle:
        continue
    t = e["transition"]
    state["status"], state["updated_at"] = t["status"], t["at"]
    turn = e.get("round")
    if isinstance(turn, dict):
        state["turns"] = [x for x in state["turns"] if x["round"] != turn["round"]] + [turn]
        state["turns"].sort(key=lambda x: x["round"])
        if t["event"] == "round_ended" and turn["outcome"] == "completed":
            state["self_report"] = turn["self_report"]
    if "plan" in e:
        state["plan"] = e["plan"]
    if "checks" in e:
        state["checks"], state["rounds_used"] = e["checks"], e["rounds_used"]
    if e["type"] == "task_finished":
        state["reason"] = e["reason"]
    if state["status"] not in ("planning", "running"):
        state["process"], state["interrupted"] = None, False
assert state == status, (state, status)
EOF
}
started_after() {  # started_after <previous newest>: the task a background run made
    local i id
    for i in $(seq 1 200); do
        id=$(newest)
        [ -n "$id" ] && [ "$id" != "$1" ] && { echo "$id"; return 0; }
        sleep 0.1
    done
    return 1
}

# --- the full cycle ----------------------------------------------------------
script cycle '{"text": "1. Work out the answer.\n2. Say it."},
              {"text": "Still working it out.\nTASK STATUS: NOT DONE"},
              {"text": "It is 42.\nTASK STATUS: DONE"}'
"$APOGEE_BIN" task run "Find the answer" --require 42 </dev/null >"$WORK_DIR/cycle.txt" 2>&1 \
    || fail "the cycle did not finish done: $(cat "$WORK_DIR/cycle.txt")"
CYCLE=$(newest)
[ -n "$CYCLE" ] || fail "no task listed after the cycle"
grep -q "task $CYCLE done -- every check passed and the model reported the task done, in 2 of 8 rounds" \
    "$WORK_DIR/cycle.txt" || fail "the cycle's outcome line: $(cat "$WORK_DIR/cycle.txt")"
SEQUENCE=$(field "$CYCLE" "' '.join(x['event'] + ('@' + str(x['round']) if x.get('round') else '') + ':' + x['status'] for x in t['transitions'])")
EXPECTED="created:planning started:planning plan_started:planning plan_recorded:running round_started@1:running round_ended@1:running round_started@2:running round_ended@2:done finished:done"
[ "$SEQUENCE" = "$EXPECTED" ] || fail "the ledger's transitions:
  got      $SEQUENCE
  expected $EXPECTED"
[ "$(field "$CYCLE" "t['plan']")" = "1. Work out the answer.
2. Say it." ] || fail "the plan was not recorded: $(field "$CYCLE" "t['plan']")"
# The corrective round named what failed, in the conversation itself.
SESSION=$(session_of "$CYCLE")
python3 - "$SESSION" "$CYCLE" <<'EOF' || fail "the conversation is not the task's three turns"
import json, sys
s = json.load(open(sys.argv[1]))
assert s["task"] == sys.argv[2], s.get("task")
users = [m["content"] for m in s["messages"] if m["role"] == "user"]
assert s["turns"] == 3 and len(users) == 3, (s["turns"], len(users))
assert users[0].startswith("Task: Find the answer"), users[0]
assert users[1].startswith("Round 1 of 8: carry out your plan now."), users[1]
assert users[2].startswith("Round 2 of 8. The task is not done"), users[2]
assert '- FAILED: the answer contains "42" -- not in the answer' in users[2], users[2]
EOF

# --- status and list ---------------------------------------------------------
STATUS=$("$APOGEE_BIN" task status "$CYCLE" </dev/null 2>&1) || fail "task status: $STATUS"
for want in "task $CYCLE  done" "conversation:  $(field "$CYCLE" "t['session_id']")" \
    "rounds:        2 of 8 used" "[x] the answer contains \"42\" -- found in the answer" \
    "[x] the model reports the task done" "  1. Work out the answer."; do
    echo "$STATUS" | grep -qF -- "$want" || fail "task status lacks '$want':
$STATUS"
done

# --- an ask-level tool, unattended: denied, recorded, nothing prompts ---------
script denied '{"text": "1. Write the report."},
               {"text": "", "tool_calls": [{"name": "write_file", "arguments": {"path": "report.txt", "content": "the report"}}]},
               {"text": "The write came back: {{last_tool_result}}\nTASK STATUS: DONE"}'
START=$SECONDS
# stdin a pipe held open for a minute: a prompt would wait on it.
mkfifo "$WORK_DIR/stdin.fifo" || fail "mkfifo"
sleep 60 >"$WORK_DIR/stdin.fifo" 2>/dev/null &
HOLDER=$!
"$APOGEE_BIN" task run "Write the report" -m denied --tools --rounds 1 \
    --require-file report.txt <"$WORK_DIR/stdin.fifo" >"$WORK_DIR/denied.txt" 2>&1
CODE=$?
kill "$HOLDER" 2>/dev/null
wait "$HOLDER" 2>/dev/null
[ "$CODE" = 1 ] || fail "a task that cannot pass exited $CODE: $(cat "$WORK_DIR/denied.txt")"
[ $((SECONDS - START)) -lt 30 ] || fail "the denied task waited on its input"
[ -e report.txt ] && fail "a denied write wrote report.txt"
DENIED=$(newest)
grep -q "\[task\] denied: write_file on report.txt -- nobody is present to allow it" \
    "$WORK_DIR/denied.txt" || fail "the denial was not said: $(cat "$WORK_DIR/denied.txt")"
grep -q "The write came back: Error: the user denied permission to run 'write_file'" \
    "$WORK_DIR/denied.txt" || fail "the denial was not the tool's result, the turn going on"
grep -qi "\[y\]es" "$WORK_DIR/denied.txt" && fail "the task prompted"
[ "$(field "$DENIED" "t['rounds'][-1]['denied']")" = "[{'by': 'nobody', 'target': 'report.txt', 'tool': 'write_file'}]" ] \
    || fail "the ledger does not record the denial: $(field "$DENIED" "t['rounds'][-1]")"
# The budget: an honest status naming the check -- the model's own "done"
# claimed nothing.
[ "$(field "$DENIED" "t['status']")" = exhausted ] || fail "the denied task is not exhausted"
field "$DENIED" "t['reason']" | grep -q "the round budget (1) is spent and the task is not done -- not passed: the file .*report.txt exists and is not empty (no such file)" \
    || fail "exhaustion does not name the check: $(field "$DENIED" "t['reason']")"
"$APOGEE_BIN" task status "$DENIED" </dev/null 2>&1 | grep -q "round 1: write_file on report.txt" \
    || fail "task status does not show the denial"

# --- 27i: a granted write runs unprompted, recorded with its target -------------
script granted '{"text": "1. Write the report."},
                {"text": "", "tool_calls": [{"name": "write_file", "arguments": {"path": "granted.txt", "content": "the report"}}]},
                {"text": "The write came back: {{last_tool_result}}\nTASK STATUS: DONE"}'
"$APOGEE_BIN" task run "Write the granted report" -m granted --tools --allow write_file \
    --rounds 1 --require-file granted.txt </dev/null >"$WORK_DIR/granted.out" 2>&1 \
    || fail "the granted task did not finish done: $(cat "$WORK_DIR/granted.out")"
[ -s granted.txt ] || fail "the granted write wrote nothing"
grep -qi "\[y\]es" "$WORK_DIR/granted.out" && fail "the granted task prompted"
grep -q "\[task\] write_file on granted.txt -- allowed by this task's grant" "$WORK_DIR/granted.out" \
    || fail "the grant's use was not said: $(cat "$WORK_DIR/granted.out")"
GRANTED=$(newest)
[ "$(field "$GRANTED" "t['policy']['grants']")" = "['write_file']" ] \
    || fail "the ledger does not record the grant: $(field "$GRANTED" "t['policy']")"
[ "$(field "$GRANTED" "t['rounds'][-1]['allowed']")" = "[{'by': 'grant', 'target': 'granted.txt', 'tool': 'write_file'}]" ] \
    || fail "the ledger does not record the grant's use: $(field "$GRANTED" "t['rounds'][-1]")"
STATUS=$("$APOGEE_BIN" task status "$GRANTED" </dev/null 2>&1)
for want in "grants:        write_file" "allowed:" "  round 1: write_file on granted.txt -- this task's grant"; do
    echo "$STATUS" | grep -qF -- "$want" || fail "task status lacks '$want':
$STATUS"
done

# --- 27i: a declared answer, consumed and recorded; without it, a failure ----------
ASK='{"text": "1. Ask which colour."},
     {"text": "", "tool_calls": [{"name": "ask_user", "arguments": {"questions": [{"header": "Colour", "question": "Which colour?", "options": [{"label": "Red"}, {"label": "Green"}]}]}}]},
     {"text": "Told: {{last_tool_result}}\nTASK STATUS: DONE"}'
script asked "$ASK"
"$APOGEE_BIN" task run "Pick a colour" -m asked --tools --on-question 'answer:"blue"' --require blue \
    </dev/null >"$WORK_DIR/asked.out" 2>&1 \
    || fail "the question-asking task did not finish done: $(cat "$WORK_DIR/asked.out")"
ASKED=$(newest)
[ "$(field "$ASKED" "t['rounds'][-1]['answered']")" = "[{'answer': 'blue', 'by': 'declared', 'question': 'Which colour?'}]" ] \
    || fail "the ledger does not record the answered question: $(field "$ASKED" "t['rounds'][-1]")"
grep -q "\[task\] a question answered with the declared answer: Which colour?" "$WORK_DIR/asked.out" \
    || fail "the answered question was not said: $(cat "$WORK_DIR/asked.out")"
STATUS=$("$APOGEE_BIN" task status "$ASKED" </dev/null 2>&1)
for want in 'on question:   answer "blue" -- every question gets this declared answer' \
    'round 1: Which colour? -- "blue", the declared answer'; do
    echo "$STATUS" | grep -qF -- "$want" || fail "task status lacks '$want':
$STATUS"
done
script asked "$ASK"
"$APOGEE_BIN" task run "Pick a colour" -m asked --tools --require blue \
    </dev/null >"$WORK_DIR/unasked.out" 2>&1 && fail "a question nobody answered ended done"
UNASKED=$(newest)
[ "$(field "$UNASKED" "t['status']")" = failed ] || fail "the unanswered task did not fail"
[ "$(field "$UNASKED" "t['reason']")" = "round 1: the model asked a question and no one is present to answer it: Which colour?" ] \
    || fail "the failure does not name the question: $(field "$UNASKED" "t['reason']")"

# --- 27i: a grant wider than the config or the agent is refused, naming the rule ----
printf 'permissions:\n  delete_file: deny\n' >>"$APOGEE_HOME/config/config.yaml"
BEFORE=$(newest)
"$APOGEE_BIN" task run "Delete it" -m granted --tools --allow delete_file </dev/null \
    >"$WORK_DIR/wide.out" 2>&1 && fail "a grant the config denies ran"
grep -q "\-\-allow delete_file: the config says permissions.delete_file: deny -- a task's grant is never wider than the config" \
    "$WORK_DIR/wide.out" || fail "the config's refusal: $(cat "$WORK_DIR/wide.out")"
"$APOGEE_BIN" task run "Write it" -m granted --tools --agent security-review --allow write_file \
    </dev/null >"$WORK_DIR/wide.out" 2>&1 && fail "a grant the agent does not allow ran"
grep -q "the agent 'security-review' runs with tools: read-only, which leaves out write_file -- a task's grant is never wider than its agent's policy" \
    "$WORK_DIR/wide.out" || fail "the agent's refusal: $(cat "$WORK_DIR/wide.out")"
[ "$(newest)" = "$BEFORE" ] || fail "a refused grant made a task"

# --- 27j: machine mode -- the task's lifecycle on stdout, one event per transition ---
script machine '{"text": "1. Write the report.\n2. Say 42."},
                {"text": "", "tool_calls": [{"name": "write_file", "arguments": {"path": "machine.txt", "content": "the report"}}]},
                {"text": "Not yet.\nTASK STATUS: NOT DONE"},
                {"text": "It is 42.\nTASK STATUS: DONE"}'
"$APOGEE_BIN" task run "Write and say" -m machine --tools --allow write_file --require 42 \
    --output-format stream-json </dev/null >"$WORK_DIR/machine.jsonl" 2>"$WORK_DIR/machine.err" \
    || fail "the machine-mode task did not finish done: $(cat "$WORK_DIR/machine.err")"
MACHINE=$(newest)
grep -q "task $MACHINE done" "$WORK_DIR/machine.err" || fail "the outcome did not go to stderr"
grep -q '\[task\]' "$WORK_DIR/machine.jsonl" && fail "a person's line reached machine mode's stdout"
"$APOGEE_BIN" task status "$MACHINE" --output-format json </dev/null >"$WORK_DIR/machine-status.json" \
    2>"$WORK_DIR/machine-status.err" || fail "task status --output-format json: $(cat "$WORK_DIR/machine-status.err")"
[ -s "$WORK_DIR/machine-status.err" ] && fail "task status --output-format json wrote to stderr"
stream_check "$WORK_DIR/machine.jsonl" "$(ledger "$MACHINE")" "$WORK_DIR/machine-status.json" 0 \
    || fail "the machine-mode stream does not match the ledger"
python3 - "$WORK_DIR/machine.jsonl" "$MACHINE" <<'EOF' || fail "the stream's turns or grant"
import json, sys
events = [json.loads(line) for line in open(sys.argv[1])]
kinds = [e["type"] for e in events]
# Three turns -- the plan and two rounds -- each ended in its result.
assert kinds.count("result") == 3, kinds
assert "answer_delta" in kinds, kinds
grants = [e for e in events if e["type"] == "task_grant"]
assert grants == [{"type": "task_grant", "task_id": sys.argv[2], "round": 1,
                   "tool": "write_file", "target": "machine.txt", "by": "grant"}], grants
# The grant's event before the round it was used in ended.
ended = next(i for i, e in enumerate(events) if e["type"] == "task_round"
             and e["transition"]["event"] == "round_ended")
assert kinds.index("task_grant") < ended
EOF
# The listing as a document: the human list's rows, newest first.
"$APOGEE_BIN" task list --output-format json </dev/null >"$WORK_DIR/list.json" 2>/dev/null \
    || fail "task list --output-format json"
"$APOGEE_BIN" task list </dev/null >"$WORK_DIR/list.txt" 2>/dev/null || fail "task list"
python3 - "$WORK_DIR/list.json" "$WORK_DIR/list.txt" <<'EOF' || fail "task list's document and rows differ"
import json, sys
document = json.load(open(sys.argv[1]))
rows = [line.split("  ")[:3] for line in open(sys.argv[2]).read().splitlines()]
assert document["object"] == "list" and document["total"] == len(document["data"])
assert [[d["id"], d["status"], "%d/%d rounds" % (d["rounds_used"], d["rounds_budget"])]
        for d in document["data"]] == rows, (document, rows)
EOF

# --- the breaker ----------------------------------------------------------------
script stalled '{"text": "1. Try."}, {"text": "No luck."}, {"text": "Still no luck."}'
"$APOGEE_BIN" task run "Find the word" -m stalled --require "eureka" --require "found" --rounds 5 \
    </dev/null >"$WORK_DIR/stalled.txt" 2>&1 && fail "a stalled task exited 0"
STALLED=$(newest)
[ "$(field "$STALLED" "t['status']")" = stalled ] || fail "the breaker did not trip: $(cat "$WORK_DIR/stalled.txt")"
grep -q "task $STALLED stalled -- no progress in 2 rounds -- no check newly passed and no new tool call ran -- not passed: the answer contains \"eureka\" (not in the answer); not passed: the answer contains \"found\" (not in the answer); not reported done by the model" \
    "$WORK_DIR/stalled.txt" || fail "the breaker's status: $(cat "$WORK_DIR/stalled.txt")"

LIST=$("$APOGEE_BIN" task list </dev/null 2>&1)
for want in "$CYCLE  done  2/8 rounds  Find the answer" "$DENIED  exhausted  1/1 rounds" \
    "$STALLED  stalled  2/5 rounds"; do
    echo "$LIST" | grep -qF -- "$want" || fail "task list lacks '$want':
$LIST"
done

# --- the lock, and a task killed mid-round ------------------------------------------
# Round 2 streams slowly: long enough to try a second run, then kill it.
script killed '{"text": "1. Answer."}, {"text": "Not yet.\nTASK STATUS: NOT DONE"},
               {"text": "This answer streams slowly and never finishes here.", "delay_ms": 400}'
BEFORE=$(newest)
"$APOGEE_BIN" task run "Answer slowly" -m killed --require 42 </dev/null >"$WORK_DIR/killed.txt" 2>&1 &
RUNNER=$!
KILLED=$(started_after "$BEFORE") || fail "the background task never started"
wait_for "$KILLED" '"round": 2' || fail "the slow task never reached round 2"
"$APOGEE_BIN" task run "A second task" </dev/null >"$WORK_DIR/second.txt" 2>&1 \
    && fail "a second task ran beside a live one"
grep -q "task $KILLED is running (process $RUNNER) -- one task runs at a time" "$WORK_DIR/second.txt" \
    || fail "the lock did not name the running task: $(cat "$WORK_DIR/second.txt")"
"$APOGEE_BIN" task status </dev/null 2>&1 | grep -q "task $KILLED  running (now, process $RUNNER)" \
    || fail "status does not show the running task"
kill -9 "$RUNNER"
wait "$RUNNER" 2>/dev/null
"$APOGEE_BIN" task status "$KILLED" </dev/null 2>&1 | grep -q "running (interrupted -- its process is gone" \
    || fail "status does not say the killed task was interrupted"
# The next round's answer, as the restarted model gives it.
script killed '{"text": "It is 42.\nTASK STATUS: DONE"}'
"$APOGEE_BIN" task resume "$KILLED" </dev/null >"$WORK_DIR/resumed.txt" 2>&1 \
    || fail "the killed task did not resume to done: $(cat "$WORK_DIR/resumed.txt")"
[ "$(field "$KILLED" "t['status']")" = done ] || fail "the resumed task is not done"
python3 - "$(session_of "$KILLED")" <<'EOF' || fail "the resumed conversation said a turn twice"
import json, sys
s = json.load(open(sys.argv[1]))
users = [m["content"] for m in s["messages"] if m["role"] == "user"]
assert s["turns"] == 3, s["turns"]
assert len(users) == 3 and len(set(users)) == 3, users
assert users[2].startswith("Round 2 of 8."), users[2]
EOF
[ "$(field "$KILLED" "[x['event'] for x in t['transitions']].count('round_started')")" = 3 ] \
    || fail "round 2 was not started again after the kill"
[ "$(field "$KILLED" "'resumed' in [x['event'] for x in t['transitions']]")" = True ] \
    || fail "the resume is not in the ledger"

# --- task cancel: the running turn ends through the loop's cancellation ---------------
script cancelled '{"text": "1. Take a long time."},
                  {"text": "This answer streams slowly enough to be cancelled while it does.", "delay_ms": 300}'
BEFORE=$(newest)
"$APOGEE_BIN" task run "Take your time" -m cancelled --require 42 </dev/null >"$WORK_DIR/cancelled.txt" 2>&1 &
RUNNER=$!
CANCELLED=$(started_after "$BEFORE") || fail "the background task never started"
wait_for "$CANCELLED" '"round": 1' || fail "the task to cancel never reached round 1"
"$APOGEE_BIN" task cancel </dev/null >"$WORK_DIR/cancel.txt" 2>&1 || fail "task cancel: $(cat "$WORK_DIR/cancel.txt")"
grep -q "asked task $CANCELLED to cancel" "$WORK_DIR/cancel.txt" || fail "cancel did not ask the runner"
wait "$RUNNER"
CODE=$?
[ "$CODE" = 130 ] || fail "a cancelled task exited $CODE: $(cat "$WORK_DIR/cancelled.txt")"
[ "$(field "$CANCELLED" "t['reason']")" = "cancelled by 'apogee task cancel' during round 1" ] \
    || fail "the cancel's reason: $(field "$CANCELLED" "t['reason']")"
# The turn cut short left no trace: the conversation is the plan alone.
python3 -c "import json,sys; s=json.load(open(sys.argv[1])); assert s['turns']==1 and len(s['messages'])==2, s" \
    "$(session_of "$CANCELLED")" || fail "a cancelled turn was left in the conversation"
"$APOGEE_BIN" chats delete "$(field "$CANCELLED" "t['session_id']")" </dev/null >/dev/null 2>&1 \
    || fail "a cancelled task's conversation could not be deleted"

# --- task halt: stopped when its round ends; a live task's chat kept --------------------
script halted '{"text": "1. Answer."},
               {"text": "Round one streams slowly, and ends.\nTASK STATUS: NOT DONE", "delay_ms": 200}'
BEFORE=$(newest)
"$APOGEE_BIN" task run "Answer when asked" -m halted --require 42 </dev/null >"$WORK_DIR/halted.txt" 2>&1 &
RUNNER=$!
HALTED=$(started_after "$BEFORE") || fail "the background task never started"
wait_for "$HALTED" '"round": 1' || fail "the task to halt never reached round 1"
"$APOGEE_BIN" task halt "$HALTED" </dev/null >"$WORK_DIR/halt.txt" 2>&1 || fail "task halt: $(cat "$WORK_DIR/halt.txt")"
wait "$RUNNER"
[ "$(field "$HALTED" "t['status']")" = halted ] || fail "the task did not halt: $(cat "$WORK_DIR/halted.txt")"
[ "$(field "$HALTED" "t['reason']")" = "halted by 'apogee task halt' after round 1" ] \
    || fail "the halt's reason: $(field "$HALTED" "t['reason']")"
HALTED_CHAT=$(field "$HALTED" "t['session_id']")
"$APOGEE_BIN" chats delete "$HALTED_CHAT" </dev/null >"$WORK_DIR/delete.txt" 2>&1 \
    && fail "a live task's conversation was deleted"
grep -q "is the conversation of task $HALTED, which is halted" "$WORK_DIR/delete.txt" \
    || fail "chats delete did not name the task: $(cat "$WORK_DIR/delete.txt")"
# Resumed in machine mode (27j): the stream opens with task_started, resumed,
# carrying everything the ledger held before -- a reconnecting front-end
# needs no other source.
script halted '{"text": "It is 42.\nTASK STATUS: DONE"}'
"$APOGEE_BIN" task resume "$HALTED" --output-format stream-json </dev/null \
    >"$WORK_DIR/unhalted.jsonl" 2>"$WORK_DIR/unhalted.txt" \
    || fail "the halted task did not resume to done: $(cat "$WORK_DIR/unhalted.txt")"
"$APOGEE_BIN" task status "$HALTED" --output-format json </dev/null >"$WORK_DIR/unhalted-status.json" \
    2>/dev/null || fail "task status --output-format json of the resumed task"
stream_check "$WORK_DIR/unhalted.jsonl" "$(ledger "$HALTED")" "$WORK_DIR/unhalted-status.json" 1 \
    || fail "the resumed task's stream does not open with its whole history"

# --- completion offers the tasks -----------------------------------------------------
OFFERED=$("$APOGEE_BIN" __complete task status "" </dev/null 2>/dev/null)
echo "$OFFERED" | grep -q "$CYCLE" || fail "task ids are not offered: $OFFERED"
# A finished task is nothing to halt, cancel or resume, so none offers it.
for verb in halt cancel resume; do
    OFFERED=$("$APOGEE_BIN" __complete task "$verb" "" </dev/null 2>/dev/null)
    echo "$OFFERED" | grep -q "$HALTED" && fail "task $verb offers the finished $HALTED: $OFFERED"
done
# Resume offers the stalled task in the folder it was started in, and
# nothing from any other folder -- where resume would refuse it.
OFFERED=$("$APOGEE_BIN" __complete task resume "" </dev/null 2>/dev/null)
echo "$OFFERED" | grep -q "$STALLED" || fail "task resume does not offer $STALLED: $OFFERED"
OFFERED=$(cd "$WORK_DIR" && "$APOGEE_BIN" __complete task resume "" </dev/null 2>/dev/null)
[ -z "$OFFERED" ] || fail "task resume offers tasks started in another folder: $OFFERED"

"$APOGEE_BIN" check </dev/null >"$WORK_DIR/check.txt" 2>&1 || fail "check after the tasks: $(cat "$WORK_DIR/check.txt")"
echo "task lifecycle OK"
