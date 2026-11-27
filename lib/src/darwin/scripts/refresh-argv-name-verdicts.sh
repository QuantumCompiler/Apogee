#!/usr/bin/env bash
# refresh-argv-name-verdicts.sh -- record what a built `apogee` binary says
# to every line the app builds with a dash-led name, deliberately (GUI M23).
#
#   refresh-argv-name-verdicts.sh <apogee-binary>
#
# The CLI's parser reads a word that begins with a dash, before `--`, as an
# option: `agents create -h` prints the verb's help with exit 0, and
# `embed info -x` exits 106. So the app builds every line with `Argv.line`,
# its names after `--`, and each place's names suite holds every line it
# builds -- with the names `-x`, `-h` and `--graph` -- to the verdicts this
# script records: a line never recorded fails there, and so does a verdict
# that is the parser's refusal (`help`, `exit 106`, `exit 109`).
#
# The binary is named, never defaulted: the dev channel's law keeps
# development off the production install, so this never reaches for
# ~/.local/bin/apogee. HOME and APOGEE_HOME are both a throwaway sandbox,
# `env -i` clears the rest, stdin is /dev/null, every run is denied the
# network (sandbox-exec) and given 60 seconds, and nothing of ~/.apogee is
# touched.
#
# Rewrites tests/presentation/shell/Fixtures/argv-name-verdicts.txt: each row
# it already holds (a verdict, a tab, the line's words joined by a space, an
# empty word written ""; a new line is added as `?<TAB><line>`) run again in a
# copy of the sandbox of its own, and given the binary's verdict: `help` where
# it printed the verb's usage, else `exit <status>: <first line>`.
# /Users/someone in a line is the copy's home, and is written back so.
#
# The sandbox: the shipped config, a mock backend, and in every section the
# app lists -- backends, agents, MCP servers, suites, review targets,
# symphonies -- the entries `-x`, `-h` and `--graph`, hand-written as an
# older or hand-edited config keeps them; the stores and a dataset of those
# names, which the CLI itself writes after `--`; a file to ingest and a
# source tree.
set -euo pipefail
set -f # a line's words are never globs

[[ $# -eq 1 ]] || { echo "usage: refresh-argv-name-verdicts.sh <apogee-binary>" >&2; exit 2; }
binary="$1"
[[ -x "$binary" ]] || { echo "refresh-argv-name-verdicts.sh: no executable apogee at ${binary}" >&2; exit 1; }
app_dir="$(cd "$(dirname "$0")/.." && pwd)"
verdicts="${app_dir}/tests/presentation/shell/Fixtures/argv-name-verdicts.txt"
[[ -f "$verdicts" ]] || { echo "refresh-argv-name-verdicts.sh: no ${verdicts#"${app_dir}/"} to refresh" >&2; exit 1; }

work="$(mktemp -d "${TMPDIR:-/tmp}/apogee-argv-names.XXXXXX")"
trap 'rm -rf "$work"' EXIT
work="$(cd "$work" && pwd -P)"
# A run may write nowhere but the sandbox and the system's temporary and
# device paths, so a line naming a real path cannot touch it.
profile="(version 1)(allow default)(deny network-outbound (remote ip))(deny file-write* (require-not (require-any (subpath \"${work}\") (subpath \"/dev\") (subpath \"/private/var/folders\") (subpath \"/private/tmp\"))))"

# The binary in home $1, with the rest as its words: nothing of the person's
# environment, no network, and no more than 60 seconds.
in_home() {
    local home="$1"
    shift
    env -i HOME="$home" APOGEE_HOME="$home/.apogee" PATH=/usr/bin:/bin \
        perl -e 'alarm 60; exec @ARGV or die' \
        sandbox-exec -p "$profile" \
        "$binary" "$@" </dev/null
}
rewrite() { sed -e "s#$1#/Users/someone#g"; }

seed="${work}/seed"
home="${seed}/home"
mkdir -p "$home/.apogee" "$home/docs" "$home/code"
printf 'The quick brown fox jumps over the lazy dog.\n' >"$home/docs/a.txt"
printf 'int add(int a, int b) { return a + b; }\n' >"$home/code/main.c"
in_home "$home" check --fix >/dev/null 2>&1 || true
python3 - "$home/.apogee/config/config.json" <<'EOF'
import sys
path = sys.argv[1]
text = open(path).read()
names = ["-x", "-h", "--graph"]
def entries(make):
    return ",\n".join(f'    "{name}": {make}' for name in names)
sections = {
    "agents": '{ "tools": "none", "prompts": ["prompts/dash.txt"] }',
    "mcp_servers": '{ "command": "/bin/echo", "args": [], "enabled": true }',
    "suites": '{ "description": "dash", "members": { "chat": "mock" } }',
    "review": '{ "repo": "o/r" }',
    "symphonies": '{ "description": "dash", "stages": [ { "name": "one", "role": "chat", "prompt": "Say {{input}}" } ] }',
}
block = "".join(f'  "{key}": {{\n{entries(make)}\n  }},\n' for key, make in sections.items())
anchor = '{\n  // Role pointers.'
assert text.count(anchor) == 1, "the shipped config's opening changed"
text = text.replace(anchor, "{\n" + block + "  // Role pointers.", 1)
backends = '"backends": {'
assert text.count(backends) == 1, "the shipped config's backends changed"
at = text.index(backends) + len(backends)
rest = text[at:].lstrip()
while rest.startswith("//"):
    rest = rest[rest.index("\n") + 1:].lstrip()
more = "" if rest.startswith("}") else ","
added = ['    "mock": { "type": "mock" }'] + [f'    "{name}": {{ "type": "mock" }}' for name in names]
text = text[:at] + "\n" + ",\n".join(added) + more + text[at:]
open(path, "w").write(text)
EOF
printf 'Say it.\n' >"$home/.apogee/prompts/dash.txt"
in_home "$home" config set-default -- mock >/dev/null
for name in -x -h --graph; do
    in_home "$home" embed ingest -q --retriever lexical -- "$name" "$home/docs/a.txt" >/dev/null 2>&1 || true
    in_home "$home" datasets create --from empty -- "$name" >/dev/null 2>&1 || true
done

out="${work}/verdicts.txt"
: >"$out"
count=0
while IFS=$'\t' read -r _ line; do
    [[ -n "$line" ]] || continue
    copy="${work}/run-${count}"
    cp -R "$seed" "$copy"
    words=()
    for word in $line; do
        if [[ "$word" == '""' ]]; then words+=(""); else words+=("${word//\/Users\/someone/${copy}/home}"); fi
    done
    answer="$(in_home "${copy}/home" "${words[@]}" 2>&1)" && status=0 || status=$?
    answer="$(printf '%s\n' "$answer" | rewrite "${copy}/home")"
    if [[ $'\n'"$answer" == *$'\n''Usage: apogee '* ]]; then
        verdict="help"
    else
        verdict="exit ${status}: ${answer%%$'\n'*}"
    fi
    printf '%s\t%s\n' "$verdict" "$line" >>"$out"
    count=$((count + 1))
    rm -rf "$copy"
done <"$verdicts"
mv "$out" "$verdicts"

echo "recorded ${count} lines from $(in_home "${seed}/home" version | sed -n 1p) to ${verdicts#"${app_dir}/"}"
