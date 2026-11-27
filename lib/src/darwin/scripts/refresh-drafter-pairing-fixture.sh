#!/usr/bin/env bash
# refresh-drafter-pairing-fixture.sh -- record which drafter pairings a real
# `apogee` binary takes, deliberately (the GUI's 3b and M5; a refresh path
# since GUI M23).
#
#   refresh-drafter-pairing-fixture.sh <apogee-binary>
#
# Stored weights' Pair with its base… and the Backends tab's Pair a
# drafter… build `config set-backend --draft <drafter> -- <base>`; whether the
# binary pairs them is its rule -- the drafter's width and family against the
# base's, and the base built by mlx-vlm -- never the app's. DrafterPairingTests
# and BackendsKindAndDrafterTests hold every pairing line to the verdicts
# recorded here.
#
# The binary is named, never defaulted: the dev channel's law keeps
# development off the production install, so this never reaches for
# ~/.local/bin/apogee. HOME and APOGEE_HOME are both a throwaway sandbox,
# `env -i` clears the rest, stdin is /dev/null, every run is denied the
# network (sandbox-exec), and nothing of ~/.apogee is touched.
#
# Rewrites tests/presentation/models/Fixtures/drafter-pairing-verdicts.txt:
# each line it already holds (a tab after the verdict; a new line is added as
# `?<TAB><line>`, an empty word written ""), run again in a copy of the
# sandbox of its own and given the binary's verdict: `taken`, or `refused:
# <its first line>`. /Users/someone in a line is the copy's home, and is
# written back so.
#
# The sandbox holds the files the CLI reads to decide a pairing, each config
# one of the CLI's own test configs (tests/support/draft_head_configs.h):
# mlx-vlm's builds of Gemma 4 E4B (2560 wide) and 12B (3840), their
# assistants' drafters (each record naming its base), mlx-lm's Qwen3, an mlx
# backend over each base, and a placeholder Python environment holding
# mlx-lm's marker and mlx-vlm's drafter modules.
set -euo pipefail
set -f # a line's words are never globs

[[ $# -eq 1 ]] || { echo "usage: refresh-drafter-pairing-fixture.sh <apogee-binary>" >&2; exit 2; }
binary="$1"
[[ -x "$binary" ]] || { echo "refresh-drafter-pairing-fixture.sh: no executable apogee at ${binary}" >&2; exit 1; }
app_dir="$(cd "$(dirname "$0")/.." && pwd)"
verdicts="${app_dir}/tests/presentation/models/Fixtures/drafter-pairing-verdicts.txt"
[[ -f "$verdicts" ]] || { echo "refresh-drafter-pairing-fixture.sh: no ${verdicts#"${app_dir}/"} to refresh" >&2; exit 1; }

work="$(mktemp -d "${TMPDIR:-/tmp}/apogee-drafter-pairing.XXXXXX")"
trap 'rm -rf "$work"' EXIT
work="$(cd "$work" && pwd -P)"
# A run may write nowhere but the sandbox and the system's temporary and
# device paths, so a line naming a real path cannot touch it.
profile="(version 1)(allow default)(deny network-outbound (remote ip))(deny file-write* (require-not (require-any (subpath \"${work}\") (subpath \"/dev\") (subpath \"/private/var/folders\") (subpath \"/private/tmp\"))))"

in_home() {
    local home="$1"
    shift
    env -i HOME="$home" APOGEE_HOME="$home/.apogee" PATH=/usr/bin:/bin \
        sandbox-exec -p "$profile" \
        "$binary" "$@" </dev/null
}

seed="${work}/seed/home"
apogee="${seed}/.apogee"
mkdir -p "$apogee"
in_home "$seed" config init >/dev/null

set_dir() { # <dir> <config.json> <apogee-snapshot.json>
    mkdir -p "$1"
    printf '%s' "$2" >"$1/config.json"
    printf '%s' "$3" >"$1/apogee-snapshot.json"
    printf 'x' >"$1/model.safetensors"
}
models="${apogee}/models"
set_dir "${models}/Gemma4-E4B/mlx/2b3c4d5e6f70" \
    '{"architectures": ["Gemma4ForConditionalGeneration"], "model_type": "gemma4", "audio_config": {"model_type": "gemma4_audio", "hidden_size": 1536}, "vision_config": {"model_type": "gemma4_vision", "hidden_size": 768}, "text_config": {"model_type": "gemma4_text", "hidden_size": 2560, "max_position_embeddings": 131072}, "quantization": {"group_size": 64, "bits": 4}}' \
    '{"ref": "Gemma4-E4B", "source": "convert", "transform": "mlx_vlm.convert 4bit", "kind": "vision"}'
set_dir "${models}/Gemma4-12B/mlx/7a8b9c0d1e2f" \
    '{"architectures": ["Gemma4UnifiedForConditionalGeneration"], "model_type": "gemma4_unified", "audio_config": {"model_type": "gemma4_audio", "hidden_size": 1536}, "vision_config": {"model_type": "gemma4_vision", "hidden_size": 1152}, "text_config": {"model_type": "gemma4_text", "hidden_size": 3840, "max_position_embeddings": 131072}, "quantization": {"group_size": 64, "bits": 4}}' \
    '{"ref": "Gemma4-12B", "source": "convert", "transform": "mlx_vlm.convert 4bit", "kind": "vision"}'
set_dir "${models}/Qwen3-8B/mlx/1a2b3c4d5e6f" \
    '{"architectures": ["Qwen3ForCausalLM"], "model_type": "qwen3", "hidden_size": 4096, "quantization": {"group_size": 64, "bits": 4}}' \
    '{"ref": "Qwen3-8B", "source": "convert", "transform": "mlx_lm.convert 4bit", "kind": "chat"}'
set_dir "${models}/Gemma4-E4B-Assistant/mlx/4d5e6f708192" \
    '{"architectures": ["Gemma4AssistantForCausalLM"], "model_type": "gemma4_assistant", "backbone_hidden_size": 2560, "use_ordered_embeddings": true, "num_centroids": 2048, "text_config": {"model_type": "gemma4_text", "hidden_size": 256, "num_hidden_layers": 4}}' \
    '{"ref": "Gemma4-E4B-Assistant", "source": "convert", "transform": "mlx_vlm.convert", "kind": "drafter", "bases": ["Gemma4-E4B"]}'
set_dir "${models}/Gemma4-12B-Assistant/mlx/8192a3b4c5d6" \
    '{"architectures": ["Gemma4UnifiedAssistantForCausalLM"], "model_type": "gemma4_unified_assistant", "backbone_hidden_size": 3840, "text_config": {"model_type": "gemma4_unified_text", "hidden_size": 1024, "num_hidden_layers": 4}}' \
    '{"ref": "Gemma4-12B-Assistant", "source": "convert", "transform": "mlx_vlm.convert", "kind": "drafter", "bases": ["Gemma4-12B"]}'

site="${apogee}/training/venv/lib/python3.12/site-packages"
mkdir -p "${site}/mlx_lm" "${site}/mlx_vlm/speculative/drafters/gemma4_assistant" \
    "${site}/mlx_vlm/speculative/drafters/gemma4_unified_assistant" "${apogee}/training/venv/bin"
printf '__version__ = "0.30.0"\n' >"${site}/mlx_lm/__init__.py"
printf '__version__ = "0.4.0"\n' >"${site}/mlx_vlm/__init__.py"
printf '' >"${site}/mlx_vlm/speculative/__init__.py"
printf '' >"${site}/mlx_vlm/speculative/drafters/__init__.py"
printf 'class Model:\n    pass\n' >"${site}/mlx_vlm/speculative/drafters/gemma4_assistant/__init__.py"
printf 'class Model:\n    pass\n' >"${site}/mlx_vlm/speculative/drafters/gemma4_unified_assistant/__init__.py"

# An mlx backend over each base, hand-written as 3b's sandbox wrote them.
python3 - "${apogee}/config/config.json" "$models" <<'EOF'
import sys
path, models = sys.argv[1], sys.argv[2]
text = open(path).read()
anchor = '"backends": {'
assert text.count(anchor) == 1, "the shipped config's backends changed"
entries = [
    ("Gemma4-E4B-4bit", models + "/Gemma4-E4B/mlx/2b3c4d5e6f70"),
    ("Gemma4-12B-4bit", models + "/Gemma4-12B/mlx/7a8b9c0d1e2f"),
    ("Qwen3-8B-4bit", models + "/Qwen3-8B/mlx/1a2b3c4d5e6f"),
]
lines = ",\n".join(f'    "{name}": {{ "type": "mlx", "model_path": "{model}" }}' for name, model in entries)
open(path, "w").write(text.replace(anchor, anchor + "\n" + lines, 1))
EOF

out="${work}/verdicts.txt"
: >"$out"
count=0
while IFS=$'\t' read -r _ line; do
    [[ -n "$line" ]] || continue
    copy="${work}/run-${count}/home"
    mkdir -p "$copy"
    cp -R "${seed}/." "$copy"
    # The models' paths, written for the copy.
    sed -i '' -e "s#${seed}#${copy}#g" "${copy}/.apogee/config/config.json"
    words=()
    for word in $line; do
        if [[ "$word" == '""' ]]; then words+=(""); else words+=("${word//\/Users\/someone/${copy}}"); fi
    done
    answer="$(in_home "$copy" "${words[@]}" 2>&1)" && status=0 || status=$?
    first="${answer%%$'\n'*}"
    first="${first//${copy}//Users/someone}"
    if [[ $status -eq 0 ]]; then verdict="taken"; else verdict="refused: ${first}"; fi
    printf '%s\t%s\n' "$verdict" "$line" >>"$out"
    count=$((count + 1))
    rm -rf "${work}/run-$((count - 1))"
done <"$verdicts"
mv "$out" "$verdicts"

echo "recorded ${count} lines from $(in_home "$seed" version | sed -n 1p) to ${verdicts#"${app_dir}/"}"
