#!/bin/sh
# `apogee review mr` (48c) on the real binary: a fake `gh` first on PATH --
# it logs every call and answers each endpoint from the planted fixtures --
# and a scripted mock backend answering the hunt and the confirm. Three runs
# over one pull request: `--dry-run` reviews and posts nothing; a piped run
# without `--yes` reviews and is refused posting, saying how to post; `--yes`
# posts one review holding the finding, its body on gh's stdin. The run's
# document comes out on stdout alone, the narration on stderr.
#
# POSIX only, like the other shell checks. Skips (77) without python3.
set -eu

APOGEE_BIN="${1:?usage: review_e2e.sh <apogee-binary> <work-dir> <fixtures-dir>}"
WORK_DIR="${2:?usage: review_e2e.sh <apogee-binary> <work-dir> <fixtures-dir>}"
FIXTURES="${3:?usage: review_e2e.sh <apogee-binary> <work-dir> <fixtures-dir>}"

if ! command -v python3 >/dev/null 2>&1; then
    echo "review_e2e: python3 not found (it reads the documents); skipping"
    exit 77
fi

rm -rf "$WORK_DIR"
mkdir -p "$WORK_DIR/bin"
export APOGEE_HOME="$WORK_DIR/home"
unset ANTHROPIC_API_KEY OPENAI_API_KEY GEMINI_API_KEY GOOGLE_API_KEY GH_TOKEN GITHUB_TOKEN
trap 'rm -rf "$WORK_DIR"' EXIT

fail() { echo "review_e2e: $*" >&2; exit 1; }

LOG="$WORK_DIR/gh.log"
cat >"$WORK_DIR/bin/gh" <<EOF
#!/bin/sh
# A fake gh: one line per call, its arguments; a request body after it.
printf '%s\n' "gh \$*" >>"$LOG"
case " \$* " in
    *" --input - "*) printf '%s\n' "body \$(cat)" >>"$LOG" ;;
esac
case " \$* " in
    *"application/vnd.github.diff"*) cat "$FIXTURES/review/security.defect.diff" ;;
    *" --method POST "*) echo '{}' ;;
    *" --paginate "*) echo '[]' ;;
    *" api user "*) echo '{"login": "apogee-bot"}' ;;
    *" repos/acme/widget/pulls/12 "*)
        echo '{"title": "Thumbnails", "body": "Adds them.", "user": {"login": "dev"},'
        echo ' "state": "open", "draft": false, "merged_at": null,'
        echo ' "head": {"ref": "thumbs", "sha": "cafe2222cafe2222"},'
        echo ' "base": {"ref": "main", "sha": "base1111base1111"},'
        echo ' "html_url": "https://github.com/acme/widget/pull/12", "updated_at": "2026-10-18T09:00:00Z"}' ;;
    *) echo "gh: unexpected call: \$*" >&2; exit 1 ;;
esac
EOF
chmod +x "$WORK_DIR/bin/gh"
PATH="$WORK_DIR/bin:$PATH"
export PATH

# The model: the security hunt finds the planted injection; the confirm
# confirms it by saying nothing.
cat >"$WORK_DIR/reviewer.json" <<'EOF'
{"turns": [
  {"text": "{\"findings\": [{\"severity\": \"high\", \"kind\": \"correctness\", \"file\": \"app/thumbnails.py\", \"location\": \"8-10\", \"claim\": \"The file name from the request reaches a shell command.\", \"suggested_fix\": \"Pass a list, no shell.\", \"confidence\": \"high\"}]}"},
  {"text": "{\"verdicts\": []}"}
]}
EOF
"$APOGEE_BIN" config init </dev/null >/dev/null || fail "config init"
"$APOGEE_BIN" config add-backend reviewer --type mock --model-path "$WORK_DIR/reviewer.json" </dev/null >/dev/null || fail "add-backend"
"$APOGEE_BIN" config set-default reviewer </dev/null >/dev/null || fail "set-default"

review() {
    # review <name> [flags...]: one review, its document on stdout.
    name="$1"; shift
    "$APOGEE_BIN" review mr acme/widget#12 --skills security --output-format json "$@" \
        </dev/null >"$WORK_DIR/$name.json" 2>"$WORK_DIR/$name.err" \
        || fail "the $name review failed: $(cat "$WORK_DIR/$name.err")"
    python3 -c 'import json,sys; json.load(open(sys.argv[1]))' "$WORK_DIR/$name.json" \
        || fail "the $name review's stdout is not one JSON document: $(cat "$WORK_DIR/$name.json")"
}
field() { python3 -c 'import json,sys; d=json.load(open(sys.argv[1])); print(eval("d" + sys.argv[2]))' "$1" "$2"; }

# --- --dry-run: reviewed, nothing posted -------------------------------------
review dry --dry-run
[ "$(field "$WORK_DIR/dry.json" '["object"]')" = "review.run" ] || fail "not a review.run document"
[ "$(field "$WORK_DIR/dry.json" '["findings"][0]["line"]')" = "8" ] || fail "the finding is not on line 8: $(cat "$WORK_DIR/dry.json")"
[ "$(field "$WORK_DIR/dry.json" '["posted"]["reason"]')" = "dry run: nothing posted" ] || fail "the dry run's reason: $(cat "$WORK_DIR/dry.json")"
grep -q "\[review\] hunting with security" "$WORK_DIR/dry.err" || fail "the narration is missing from stderr"
if grep -q -- "--method POST" "$LOG"; then fail "the dry run posted"; fi

# --- piped without --yes: refused, how to post said --------------------------
review piped
[ "$(field "$WORK_DIR/piped.json" '["posted"]["done"]')" = "False" ] || fail "a piped run posted without --yes"
field "$WORK_DIR/piped.json" '["posted"]["reason"]' | grep -q "run with --yes to post" || fail "the refusal did not name --yes: $(cat "$WORK_DIR/piped.json")"
if grep -q -- "--method POST" "$LOG"; then fail "the refused run posted"; fi

# --- --yes: one review, the finding in it, the body on stdin -----------------
review posted --yes
[ "$(field "$WORK_DIR/posted.json" '["posted"]["done"]')" = "True" ] || fail "--yes did not post: $(cat "$WORK_DIR/posted.json")"
grep -q "^gh api --method POST -H Content-Type: application/json repos/acme/widget/pulls/12/reviews --input -$" "$LOG" || fail "the batch's argv: $(cat "$LOG")"
grep -q '^body .*"commit_id":"cafe2222cafe2222"' "$LOG" || fail "the batch did not anchor to the head: $(cat "$LOG")"
grep -q '^body .*"path":"app/thumbnails.py"' "$LOG" || fail "the comment is not in the batch"
grep -q '^body .*apogee-review:summary head=cafe2222cafe2222' "$LOG" || fail "the summary's marker is missing"
[ "$(grep -c -- '--method POST' "$LOG")" = "1" ] || fail "posted more than once"

echo "review mr: reviewed, refused unasked, posted one batch under --yes - OK"
