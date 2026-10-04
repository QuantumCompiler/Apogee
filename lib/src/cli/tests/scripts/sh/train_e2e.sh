#!/bin/sh
# The run on the real binary with the mock trainer -- no Python at all: a
# template dataset created, `train run` writing a manifest with the status
# line seen, a GGUF and a backend name refused naming --safetensors, `train
# eval --suite` with substring items passing against the mock's echo (a
# judge-less item skipping loudly), `promote --as` registering the entry
# byte-exactly (exactly the new lines added, nothing else changed) and
# writing v1.gguf that the header reader parses, a second promote to v2
# repointing the same entry in place, `rollback` to v1 deleting nothing,
# retention pruning under retain_versions, `versions`, `status`, and
# `check` green throughout. Then the pipelines item: a two-stage pipeline
# whose second stage regresses the first's suite aborting under the
# cumulative gate and resuming to complete after the fix, a regime over two
# kits with the mock backend as the teacher, and the cycle from a queue
# directory -- skipped, passing and promoting with the anchor set, failing
# on a regression and tripping the breaker at k=1, refused while halted,
# resumed.
#
# POSIX only, like the other shell checks.
set -eu

APOGEE_BIN="${1:?usage: train_e2e.sh <apogee-binary> <work-dir>}"
WORK_DIR="${2:?usage: train_e2e.sh <apogee-binary> <work-dir>}"

rm -rf "$WORK_DIR"
mkdir -p "$WORK_DIR/home"
export APOGEE_HOME="$WORK_DIR/home"
unset ANTHROPIC_API_KEY OPENAI_API_KEY GEMINI_API_KEY GOOGLE_API_KEY
trap 'rm -rf "$WORK_DIR"' EXIT

fail() { echo "train_e2e: $*" >&2; exit 1; }

"$APOGEE_BIN" config init >/dev/null || fail "config init"
"$APOGEE_BIN" check --fix >/dev/null 2>&1 || fail "check --fix"
CONFIG="$APOGEE_HOME/config/config.yaml"
TRAINING="$APOGEE_HOME/training"

# A student: a SafeTensors set where the model store keeps one --
# models/<model>/safetensors/<id>/ -- with config.json and a shard.
STUDENT="$APOGEE_HOME/models/tiny/safetensors/aaaaaaaaaaaa"
mkdir -p "$STUDENT"
printf '{"architectures": ["LlamaForCausalLM"], "model_type": "llama"}\n' > "$STUDENT/config.json"
printf 'weights' > "$STUDENT/model.safetensors"
# The file a backend serves, read back through the product itself.
served() { "$APOGEE_BIN" config get "backends.$1.model_path"; }
"$APOGEE_BIN" datasets create starter >/dev/null || fail "datasets create"

# --- the run -----------------------------------------------------------------
"$APOGEE_BIN" train run tiny --dataset starter --trainer mock --iters 5 </dev/null >"$WORK_DIR/run.out" 2>"$WORK_DIR/run.err" || fail "train run: $(cat "$WORK_DIR/run.err")"
grep -q "iter 5/5" "$WORK_DIR/run.err" || fail "the status line was not seen: $(cat "$WORK_DIR/run.err")"
RUN=$(ls "$TRAINING/runs" | head -1)
[ -n "$RUN" ] || fail "no run directory"
[ -f "$TRAINING/runs/$RUN/manifest.json" ] || fail "no manifest"
grep -q '"status": "complete"' "$TRAINING/runs/$RUN/manifest.json" || fail "manifest status: $(cat "$TRAINING/runs/$RUN/manifest.json")"
grep -q '"trainer": "mock"' "$TRAINING/runs/$RUN/manifest.json" || fail "manifest trainer"
[ -f "$TRAINING/runs/$RUN/adapters/adapters.safetensors" ] || fail "no adapter"
grep -q "run $RUN complete" "$WORK_DIR/run.out" || fail "run summary: $(cat "$WORK_DIR/run.out")"

# --- a crashed driver is a failed run, never a silent success -----------------
printf '{"mock": {"error": "GPU on fire"}}\n' > "$TRAINING/datasets/broken.jsonl"
if "$APOGEE_BIN" train run tiny --dataset broken --trainer mock --iters 3 </dev/null >/dev/null 2>"$WORK_DIR/broken.err"; then
    fail "a failing driver was a successful run"
fi
grep -q "GPU on fire" "$WORK_DIR/broken.err" || fail "the failure was not named: $(cat "$WORK_DIR/broken.err")"
BROKEN=$(ls -t "$TRAINING/runs" | head -1)
grep -q '"status": "failed"' "$TRAINING/runs/$BROKEN/manifest.json" || fail "the failed run is not recorded as failed"
"$APOGEE_BIN" train status | grep -q "failed" || fail "status hides the failed run"

# --- refusals ----------------------------------------------------------------
if "$APOGEE_BIN" train run "$APOGEE_HOME/models/tiny.gguf" --dataset starter --trainer mock </dev/null >/dev/null 2>"$WORK_DIR/gguf.err"; then
    fail "a GGUF was accepted as a student"
fi
grep -q -- "--safetensors" "$WORK_DIR/gguf.err" || fail "GGUF refusal wording: $(cat "$WORK_DIR/gguf.err")"
"$APOGEE_BIN" config add-backend local --type llamacpp --model-path /nowhere.gguf >/dev/null || fail "add-backend local"
if "$APOGEE_BIN" train run local --dataset starter --trainer mock </dev/null >/dev/null 2>"$WORK_DIR/backend.err"; then
    fail "a backend name was accepted as a student"
fi
grep -q -- "--safetensors" "$WORK_DIR/backend.err" || fail "backend refusal wording: $(cat "$WORK_DIR/backend.err")"
"$APOGEE_BIN" config delete-backend local >/dev/null || fail "delete-backend local"
if "$APOGEE_BIN" train promote "$RUN" --as tuned </dev/null >/dev/null 2>"$WORK_DIR/gate.err"; then
    fail "promote ran without an eval"
fi
grep -q "no eval results" "$WORK_DIR/gate.err" || fail "gate wording: $(cat "$WORK_DIR/gate.err")"
[ ! -d "$TRAINING/versions" ] || fail "a refused promote wrote a version"

# --- eval --------------------------------------------------------------------
printf '{"prompt": "say hello", "expected": "hello"}\n{"prompt": "count to three"}\n' > "$WORK_DIR/suite.jsonl"
"$APOGEE_BIN" train eval "$RUN" --suite "$WORK_DIR/suite.jsonl" </dev/null >"$WORK_DIR/eval.out" 2>&1 || fail "train eval: $(cat "$WORK_DIR/eval.out")"
grep -q "eval PASSED -- 100% (2/2)" "$WORK_DIR/eval.out" || fail "eval summary: $(cat "$WORK_DIR/eval.out")"
grep -q "1 item(s) with no .expected. skipped" "$WORK_DIR/eval.out" || fail "the skip was silent: $(cat "$WORK_DIR/eval.out")"
grep -q '"check_type": "contains"' "$TRAINING/runs/$RUN/manifest.json" || fail "eval not recorded"
"$APOGEE_BIN" train eval "$RUN" --suite "$WORK_DIR/suite.jsonl" </dev/null | grep -q "already ran" || fail "a second eval re-ran"
# A kit's inline suite by name resolves too.
"$APOGEE_BIN" train eval "$RUN" --suite reasoning --force </dev/null >"$WORK_DIR/kit.out" 2>&1 || fail "eval by kit: $(cat "$WORK_DIR/kit.out")"
grep -q "suite kit:reasoning" "$WORK_DIR/kit.out" || fail "kit suite label: $(cat "$WORK_DIR/kit.out")"
"$APOGEE_BIN" train eval "$RUN" --suite "$WORK_DIR/suite.jsonl" --force </dev/null >/dev/null 2>&1 || fail "eval --force"

# --- promote: byte-exact registration ------------------------------------------
cp "$CONFIG" "$WORK_DIR/before.yaml"
"$APOGEE_BIN" train promote "$RUN" --as tuned </dev/null >"$WORK_DIR/promote.out" 2>&1 || fail "train promote: $(cat "$WORK_DIR/promote.out")"
V1=$(served tuned)
[ -f "$V1" ] || fail "no v1 GGUF at '$V1'"
# In the model store, beside the model it was trained from, under its own id.
case "$V1" in
    "$APOGEE_HOME/models/tiny/gguf/"*/tuned-v1.gguf) ;;
    *) fail "v1 is not in the model store: $V1" ;;
esac
[ -f "${V1%.gguf}.json" ] || fail "the promoted GGUF has no record beside it"
[ -f "$TRAINING/versions/tuned.json" ] || fail "no ledger"
grep -q "promoted to new backend tuned -> v1" "$WORK_DIR/promote.out" || fail "promote summary: $(cat "$WORK_DIR/promote.out")"
[ ! -d "$TRAINING/runs/$RUN/fused" ] || fail "the fused checkpoint was kept without --keep-fused"
# Byte-exact: the diff is exactly the entry's three lines (plus the blank
# separator the editor adds) at the end of the backends section, and not
# one byte removed.
[ "$(diff "$WORK_DIR/before.yaml" "$CONFIG" | grep -c '^<')" = "0" ] || fail "promote removed config lines: $(diff "$WORK_DIR/before.yaml" "$CONFIG")"
diff "$WORK_DIR/before.yaml" "$CONFIG" | grep '^>' | grep -v '^> *$' > "$WORK_DIR/added.txt" || true
printf '>   tuned:\n>     type: llamacpp\n>     model_path: %s\n' "$V1" > "$WORK_DIR/expected-diff.txt"
cmp "$WORK_DIR/expected-diff.txt" "$WORK_DIR/added.txt" || fail "the config is not the previous bytes plus the entry: $(cat "$WORK_DIR/added.txt")"
[ "$("$APOGEE_BIN" config get backends.tuned.model_path)" = "$V1" ] || fail "config get model_path"
"$APOGEE_BIN" models info tuned >/dev/null 2>&1 || fail "the promoted GGUF does not inspect"

# --- a second promote repoints the same entry in place ---------------------------
"$APOGEE_BIN" train run tiny --dataset starter --trainer mock --iters 3 </dev/null >/dev/null 2>&1 || fail "second run"
RUN2=$(ls -t "$TRAINING/runs" | head -1)
[ "$RUN2" != "$RUN" ] || fail "second run id collides"
"$APOGEE_BIN" train eval "$RUN2" --suite "$WORK_DIR/suite.jsonl" </dev/null >/dev/null 2>&1 || fail "second eval"
cp "$CONFIG" "$WORK_DIR/before2.yaml"
"$APOGEE_BIN" train promote "$RUN2" --as tuned </dev/null >"$WORK_DIR/promote2.out" 2>&1 || fail "second promote: $(cat "$WORK_DIR/promote2.out")"
V2=$(served tuned)
[ -f "$V2" ] || fail "no v2 GGUF at '$V2'"
[ "$V2" != "$V1" ] || fail "a second run's weights landed on the first's file"
grep -q "updated backend tuned -> v2" "$WORK_DIR/promote2.out" || fail "second promote summary: $(cat "$WORK_DIR/promote2.out")"
sed "s|$V1|$V2|" "$WORK_DIR/before2.yaml" > "$WORK_DIR/expected2.yaml"
cmp "$WORK_DIR/expected2.yaml" "$CONFIG" || fail "the repoint changed more than the path: $(diff "$WORK_DIR/expected2.yaml" "$CONFIG")"
"$APOGEE_BIN" train versions tuned >"$WORK_DIR/versions.out" || fail "train versions"
grep -q "v2 .*<- active" "$WORK_DIR/versions.out" || fail "v2 not active: $(cat "$WORK_DIR/versions.out")"
grep -q "^  v1 " "$WORK_DIR/versions.out" || fail "v1 not listed"

# --- rollback deletes nothing ---------------------------------------------------
"$APOGEE_BIN" train rollback tuned >"$WORK_DIR/rollback.out" || fail "rollback: $(cat "$WORK_DIR/rollback.out")"
grep -q "v2 -> v1" "$WORK_DIR/rollback.out" || fail "rollback summary: $(cat "$WORK_DIR/rollback.out")"
[ "$("$APOGEE_BIN" config get backends.tuned.model_path)" = "$V1" ] || fail "rollback did not repoint"
[ -f "$V2" ] || fail "rollback deleted v2"
cmp "$WORK_DIR/before2.yaml" "$CONFIG" || fail "rollback is not the exact inverse of the repoint"
if "$APOGEE_BIN" train rollback tuned >/dev/null 2>"$WORK_DIR/rollback2.err"; then
    fail "a second rollback found somewhere to go"
fi
grep -q "nothing below it to roll back to" "$WORK_DIR/rollback2.err" || fail "rollback refusal wording: $(cat "$WORK_DIR/rollback2.err")"

# --- retention: retain_versions 1 prunes the inactive one, never the active ------
printf '\ntraining:\n  retain_versions: 1\n' >> "$CONFIG"
"$APOGEE_BIN" train promote "$RUN2" --as tuned --force </dev/null >"$WORK_DIR/promote3.out" 2>&1 || fail "third promote: $(cat "$WORK_DIR/promote3.out")"
V3=$(served tuned)
grep -q "tuned -> v3" "$WORK_DIR/promote3.out" || fail "numbers must be max + 1, never the count: $(cat "$WORK_DIR/promote3.out")"
grep -q "pruned:" "$WORK_DIR/promote3.out" || fail "nothing pruned: $(cat "$WORK_DIR/promote3.out")"
[ ! -f "$V1" ] || fail "v1 survived retain_versions 1"
# v3 is RUN2 promoted again: the same weights as v2, so one stored file --
# which v2's pruning must not take from v3.
[ "$V3" = "$V2" ] || fail "identical weights were stored twice: $V2 and $V3"
[ -f "$V3" ] || fail "pruning v2 removed the file v3 serves"
"$APOGEE_BIN" train versions tuned | grep -q "v1 .*(pruned)" || fail "the pruned entry is not shown as history"
if "$APOGEE_BIN" train rollback tuned >/dev/null 2>"$WORK_DIR/rollback3.err"; then
    fail "rollback reached a pruned version"
fi
grep -q "pruned" "$WORK_DIR/rollback3.err" || fail "pruned rollback wording: $(cat "$WORK_DIR/rollback3.err")"

# --- status and the doctor ----------------------------------------------------------
"$APOGEE_BIN" train status >"$WORK_DIR/status.out" || fail "train status"
grep -q "Runs: 3 (0 running)" "$WORK_DIR/status.out" || fail "status runs: $(cat "$WORK_DIR/status.out")"
grep -q "tuned .*active v3" "$WORK_DIR/status.out" || fail "status versions: $(cat "$WORK_DIR/status.out")"
"$APOGEE_BIN" check >"$WORK_DIR/check.out" 2>&1 || fail "check: $(cat "$WORK_DIR/check.out")"
grep -q "ok   versions: tuned" "$WORK_DIR/check.out" || fail "check ledger row: $(grep 'versions' "$WORK_DIR/check.out")"
grep -q "ok   converter" "$WORK_DIR/check.out" || fail "check converter row: $(grep converter "$WORK_DIR/check.out")"

# --- the pipelines item ---------------------------------------------------------
# The training block is rewritten once for the rest: the mock trainer, a
# named pipeline, and the cycle over a queue directory with the breaker at 1.
awk '/^training:$/{exit} {print}' "$CONFIG" > "$WORK_DIR/config.trim" && cp "$WORK_DIR/config.trim" "$CONFIG"
mkdir -p "$TRAINING/suites"
printf '{"prompt": "say hello", "expected": "hello"}\n' > "$TRAINING/suites/hello.jsonl"
printf '{"prompt": "say nope", "expected": "nope"}\n' > "$TRAINING/suites/nope.jsonl"
cat >> "$CONFIG" <<EOF2
training:
  trainer: mock
  retain_versions: 0
  pipelines:
    nightly:
      student: tiny
      stages:
        - name: base
          dataset: starter
          eval_suite: hello
          iters: 2
  cycle:
    pipeline: nightly
    backend: nightly-model
    circuit_breaker_k: 1
    sources:
      - type: directory
        dir: $WORK_DIR/queue
EOF2
"$APOGEE_BIN" check >"$WORK_DIR/check2.out" 2>&1 || fail "check with the cycle configured: $(cat "$WORK_DIR/check2.out")"
grep -q "ok   cycle " "$WORK_DIR/check2.out" || fail "check cycle row: $(grep cycle "$WORK_DIR/check2.out")"
grep -q "ok   pipeline: nightly" "$WORK_DIR/check2.out" || fail "check pipeline row: $(grep pipeline "$WORK_DIR/check2.out")"

# A two-stage pipeline whose second stage regresses the first's suite: the
# cumulative gate aborts the run with stage 1 passed and stage 2 failed;
# after the data is fixed, resume runs only stage 2 and completes; the last
# stage's run promotes like any other.
printf '{"mock": {"answer": "nope"}}\n' > "$TRAINING/datasets/regress.jsonl"
cat > "$WORK_DIR/pipe.yaml" <<EOF2
name: two
student: tiny
stages:
  - name: a
    dataset: starter
    eval_suite: hello
    iters: 2
  - name: b
    dataset: regress
    eval_suite: nope
    iters: 2
EOF2
if "$APOGEE_BIN" train pipeline run --pipeline "$WORK_DIR/pipe.yaml" </dev/null >"$WORK_DIR/pipe.out" 2>"$WORK_DIR/pipe.err"; then
    fail "a regressing stage passed the cumulative gate"
fi
grep -q "cumulative eval gate" "$WORK_DIR/pipe.err" || fail "gate wording: $(cat "$WORK_DIR/pipe.err")"
grep -q "pipeline aborted" "$WORK_DIR/pipe.out" || fail "pipeline summary: $(cat "$WORK_DIR/pipe.out")"
PIPE=$(ls "$TRAINING/pipelines" | head -1)
[ -n "$PIPE" ] || fail "no pipeline directory"
grep -q '"status": "aborted"' "$TRAINING/pipelines/$PIPE/manifest.json" || fail "pipeline manifest status"
[ -d "$TRAINING/runs/$PIPE-s0/fused" ] || fail "stage 0 was not fused for stage 1"
[ ! -d "$TRAINING/runs/$PIPE-s1/fused" ] || fail "the last stage was fused"
grep -q "\"parent_run\": \"$PIPE-s0\"" "$TRAINING/runs/$PIPE-s1/manifest.json" || fail "stage 1 lacks its parent"
"$APOGEE_BIN" train pipeline status "$PIPE" >"$WORK_DIR/pstatus.out" || fail "pipeline status"
grep -q "status:  aborted" "$WORK_DIR/pstatus.out" || fail "pipeline status table: $(cat "$WORK_DIR/pstatus.out")"
grep -q "Resume from stage 2" "$WORK_DIR/pstatus.out" || fail "pipeline status resume hint"
cp "$TRAINING/datasets/starter.jsonl" "$TRAINING/datasets/regress.jsonl"
"$APOGEE_BIN" train pipeline resume "$PIPE" --pipeline "$WORK_DIR/pipe.yaml" </dev/null >"$WORK_DIR/resume.out" 2>&1 || fail "pipeline resume: $(cat "$WORK_DIR/resume.out")"
grep -q "pipeline complete" "$WORK_DIR/resume.out" || fail "resume summary: $(cat "$WORK_DIR/resume.out")"
grep -q '"status": "complete"' "$TRAINING/pipelines/$PIPE/manifest.json" || fail "resumed manifest status"
"$APOGEE_BIN" train promote "$PIPE-s1" --as staged </dev/null >/dev/null 2>&1 || fail "promote of a stage run"
[ -f "$(served staged)" ] || fail "no staged v1 GGUF"
if "$APOGEE_BIN" train pipeline resume "$PIPE" --pipeline "$WORK_DIR/pipe.yaml" </dev/null >/dev/null 2>"$WORK_DIR/resume2.err"; then
    fail "a complete pipeline resumed"
fi
grep -q "already complete" "$WORK_DIR/resume2.err" || fail "resume refusal wording"

# A regime over two kits with the mock backend as the teacher and --no-promote:
# a dataset and a suite per kit, one gated pipeline, the promote command named.
cat > "$WORK_DIR/teacher.json" <<'EOF2'
{"turns": [{"text": "[{\"prompt\": \"say hello\", \"completion\": \"hello\"}, {\"prompt\": \"say hi\", \"completion\": \"hi\"}]"}]}
EOF2
"$APOGEE_BIN" config add-backend teacher --type mock --model-path "$WORK_DIR/teacher.json" >/dev/null || fail "add-backend teacher"
for KIT in alpha beta; do
    cat > "$TRAINING/kits/$KIT.yaml" <<EOF2
name: $KIT
description: a test kit
synth:
  system: |
    Make examples.
  seeds:
    - greetings
  count: 4
  per_seed: 2
train:
  iters: 2
eval:
  - prompt: please say hello
    expected: hello
EOF2
done
LEDGERS_BEFORE=$(ls "$TRAINING/versions" | grep -c '\.json$')
"$APOGEE_BIN" train regime run --teacher teacher --student tiny --kit alpha --kit beta --count 2 --no-promote </dev/null >"$WORK_DIR/regime.out" 2>"$WORK_DIR/regime.err" || fail "regime run: $(cat "$WORK_DIR/regime.err")"
grep -q "kit alpha: 2 example(s)" "$WORK_DIR/regime.out" || fail "regime synth summary: $(cat "$WORK_DIR/regime.out")"
grep -q "pipeline complete" "$WORK_DIR/regime.out" || fail "regime pipeline: $(cat "$WORK_DIR/regime.out")"
grep -q "Promote when ready" "$WORK_DIR/regime.out" || fail "regime promote hint"
REGIME=$(ls "$TRAINING/regime" | head -1)
[ -f "$TRAINING/regime/$REGIME/alpha.jsonl" ] || fail "no synthesised dataset"
[ -f "$TRAINING/regime/$REGIME/beta.eval.jsonl" ] || fail "no materialised suite"
[ -f "$TRAINING/pipelines/$REGIME-pipe/manifest.json" ] || fail "no regime pipeline"
[ "$(ls "$TRAINING/versions" | grep -c '\.json$')" = "$LEDGERS_BEFORE" ] || fail "--no-promote promoted"

# The cycle from a queue directory: nothing queued is skipped; a file gates,
# promotes into training.cycle.backend and sets the anchor; a regressing
# file fails, trips the breaker at k=1 and halts; resume clears it.
"$APOGEE_BIN" train cycle run </dev/null >"$WORK_DIR/cycle0.out" 2>&1 || fail "cycle run (empty): $(cat "$WORK_DIR/cycle0.out")"
grep -q "cycle skipped" "$WORK_DIR/cycle0.out" || fail "empty cycle: $(cat "$WORK_DIR/cycle0.out")"
cp "$TRAINING/datasets/starter.jsonl" "$WORK_DIR/queue/day1.jsonl"
cp "$CONFIG" "$WORK_DIR/before-cycle.yaml"
"$APOGEE_BIN" train cycle run </dev/null >"$WORK_DIR/cycle1.out" 2>&1 || fail "cycle run: $(cat "$WORK_DIR/cycle1.out")"
grep -q "cycle PASSED -- nightly-model promoted to v1" "$WORK_DIR/cycle1.out" || fail "cycle pass: $(cat "$WORK_DIR/cycle1.out")"
grep -q "anchor set to v1" "$WORK_DIR/cycle1.out" || fail "anchor not set"
[ -f "$(served nightly-model)" ] || fail "no nightly v1 GGUF"
[ -f "$WORK_DIR/queue/consumed/day1.jsonl" ] || fail "the queue file was not consumed"
[ "$(diff "$WORK_DIR/before-cycle.yaml" "$CONFIG" | grep -c '^<')" = "0" ] || fail "the cycle's promote removed config lines"
served nightly-model | grep -q "/gguf/.*/nightly-model-v1.gguf" || fail "cycle backend not registered"
"$APOGEE_BIN" train cycle status >"$WORK_DIR/cstatus.out" || fail "cycle status"
grep -q "anchor:            v1" "$WORK_DIR/cstatus.out" || fail "cycle status anchor: $(cat "$WORK_DIR/cstatus.out")"
grep -q "pass" "$WORK_DIR/cstatus.out" || fail "cycle status rows"
printf '{"mock": {"answer": "nope"}}\n' > "$WORK_DIR/queue/day2.jsonl"
if "$APOGEE_BIN" train cycle run </dev/null >"$WORK_DIR/cycle2.out" 2>&1; then
    fail "a regressing candidate passed the cycle"
fi
grep -q "cycle FAILED" "$WORK_DIR/cycle2.out" || fail "cycle fail: $(cat "$WORK_DIR/cycle2.out")"
grep -q "nothing reached inference" "$WORK_DIR/cycle2.out" || fail "cycle discard wording"
grep -q "the loop is halted" "$WORK_DIR/cycle2.out" || fail "breaker not tripped at k=1"
if ls "$APOGEE_HOME"/models/*/gguf/*/nightly-model-v2.gguf >/dev/null 2>&1; then
    fail "a failed cycle promoted"
fi
[ -f "$WORK_DIR/queue/day2.jsonl" ] || fail "a failed cycle consumed its file"
if "$APOGEE_BIN" train cycle run </dev/null >/dev/null 2>"$WORK_DIR/cycle3.err"; then
    fail "a halted cycle ran"
fi
grep -q "cycle resume" "$WORK_DIR/cycle3.err" || fail "halt wording: $(cat "$WORK_DIR/cycle3.err")"
"$APOGEE_BIN" check >"$WORK_DIR/check3.out" 2>&1 || fail "check while halted: $(cat "$WORK_DIR/check3.out")"
grep -q "warn  cycle history" "$WORK_DIR/check3.out" || fail "check halted row: $(grep 'cycle' "$WORK_DIR/check3.out")"
"$APOGEE_BIN" train cycle resume >"$WORK_DIR/resume3.out" || fail "cycle resume"
grep -q "cycle resumed" "$WORK_DIR/resume3.out" || fail "cycle resume wording"
rm "$WORK_DIR/queue/day2.jsonl"
"$APOGEE_BIN" train cycle run </dev/null | grep -q "cycle skipped" || fail "the resumed cycle did not run"
[ ! -f "$TRAINING/cycle/cycle.lock" ] || fail "the lock was left behind"
"$APOGEE_BIN" train status >"$WORK_DIR/status2.out" || fail "train status"
grep -q "Cycle: idle; backend nightly-model" "$WORK_DIR/status2.out" || fail "status cycle line: $(cat "$WORK_DIR/status2.out")"
grep -q "Pipeline: none in progress" "$WORK_DIR/status2.out" || fail "status pipeline line: $(cat "$WORK_DIR/status2.out")"
"$APOGEE_BIN" check >"$WORK_DIR/check4.out" 2>&1 || fail "final check: $(cat "$WORK_DIR/check4.out")"
grep -q "ok   cycle history" "$WORK_DIR/check4.out" || fail "check cycle history row: $(grep 'cycle' "$WORK_DIR/check4.out")"

echo "apogee train end-to-end: OK"
