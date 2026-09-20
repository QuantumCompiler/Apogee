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
# `check` green throughout.
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

# A student: a snapshot directory with config.json and a shard.
mkdir -p "$APOGEE_HOME/models/tiny"
printf '{"architectures": ["LlamaForCausalLM"], "model_type": "llama"}\n' > "$APOGEE_HOME/models/tiny/config.json"
printf 'weights' > "$APOGEE_HOME/models/tiny/model.safetensors"
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
V1="$TRAINING/versions/tuned/v1.gguf"
[ -f "$V1" ] || fail "no v1.gguf"
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
V2="$TRAINING/versions/tuned/v2.gguf"
[ -f "$V2" ] || fail "no v2.gguf"
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
V3="$TRAINING/versions/tuned/v3.gguf"
[ -f "$V3" ] || fail "no v3.gguf: numbers must be max + 1, never the count"
grep -q "pruned:" "$WORK_DIR/promote3.out" || fail "nothing pruned: $(cat "$WORK_DIR/promote3.out")"
[ ! -f "$V1" ] || fail "v1 survived retain_versions 1"
[ ! -f "$V2" ] || fail "v2 survived retain_versions 1"
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

echo "apogee train end-to-end: OK"
