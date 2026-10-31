#!/usr/bin/env bash
# refresh-store-commands-fixture.sh -- record which store commands a real
# `apogee` binary takes, deliberately (the GUI's M2).
#
#   refresh-store-commands-fixture.sh [apogee-binary]     default: ~/.local/bin/apogee
#
# The convert and quantize sheets build commands whose flags the binary's
# `--help` names one by one; which combinations it takes is a rule between
# them that only the binary knows. StoreCommandsTests holds every command the
# sheets can build to the verdicts recorded here, so a sheet that builds one
# the binary refuses fails the app's suite, as do commands it never recorded.
#
# Writes, under tests/presentation/models/Fixtures/:
#   store-command-verdicts.txt  each command it already lists (a tab after the
#     verdict; a new command is added as a line `?<TAB><command>`), run again
#     and given the binary's verdict: `taken`, or `refused: <its first line>`.
#     Convert and quantize name a set that does not exist, so `nothing at …`
#     -- every flag rule passed -- is `taken`, and nothing is converted.
#   models-rename-list.json     `models list` of a sandbox holding a GGUF family
#     (qwen3-8b-Q4KM) and an MLX backend whose level is a mixed recipe
#     (GPT-OSS-20B-mixed_4_8), the rename's rows;
#   models-rename-complete.txt  `__complete models rename ""` there: the names
#     `models rename` takes.
#
# Every command runs in a throwaway APOGEE_HOME of its own, so a rename run
# changes nothing the next one reads: nothing of ~/.apogee is touched.
set -euo pipefail

binary="${1:-$HOME/.local/bin/apogee}"
[[ -x "$binary" ]] || { echo "refresh-store-commands-fixture.sh: no executable apogee at ${binary}" >&2; exit 1; }
app_dir="$(cd "$(dirname "$0")/.." && pwd)"
fixtures="${app_dir}/tests/presentation/models/Fixtures"
verdicts="${fixtures}/store-command-verdicts.txt"
[[ -f "$verdicts" ]] || { echo "refresh-store-commands-fixture.sh: no ${verdicts#"${app_dir}/"} to refresh" >&2; exit 1; }

work="$(mktemp -d "${TMPDIR:-/tmp}/apogee-store-commands.XXXXXX")"
trap 'rm -rf "$work"' EXIT

# The sandbox: a seeded home, a GGUF family and an MLX model, each registered.
seed="${work}/seed"
mkdir -p "$seed"
APOGEE_HOME="$seed" "$binary" check --fix </dev/null >/dev/null 2>&1 || true
gguf="${seed}/models/qwen3-8b/gguf/6d0e12aa0c11"
mlx="${seed}/models/GPT-OSS-20B/mlx/524b4fa3bc6a"
mkdir -p "$gguf" "$mlx"
printf 'GGUF' >"${gguf}/qwen3-8b-Q4_K_M.gguf"
printf '%s' '{"model_type":"gpt_oss","architectures":["GptOssForCausalLM"],"quantization":{"group_size":32,"bits":4,"mode":"mxfp4","model.layers.0.mlp.router":{"group_size":64,"bits":8,"mode":"affine"}}}' >"${mlx}/config.json"
printf 'x' >"${mlx}/model.safetensors"
APOGEE_HOME="$seed" "$binary" config add-backend qwen3-8b-Q4KM --type llamacpp --model-path "${gguf}/qwen3-8b-Q4_K_M.gguf" </dev/null >/dev/null
APOGEE_HOME="$seed" "$binary" config add-backend GPT-OSS-20B-mixed_4_8 --type mlx --model-path "$mlx" </dev/null >/dev/null

APOGEE_HOME="$seed" "$binary" models list --output-format json </dev/null >"${fixtures}/models-rename-list.json"
APOGEE_HOME="$seed" "$binary" __complete models rename "" </dev/null >"${fixtures}/models-rename-complete.txt"

# Each command, in a copy of the sandbox of its own.
out="${work}/verdicts.txt"
: >"$out"
count=0
while IFS=$'\t' read -r _ command; do
    [[ -n "$command" ]] || continue
    home="${work}/run-${count}"
    cp -R "$seed" "$home"
    read -r -a words <<<"$command"
    answer="$(APOGEE_HOME="$home" "$binary" "${words[@]}" </dev/null 2>&1)" && status=0 || status=$?
    first="$(printf '%s\n' "$answer" | head -1)"
    if [[ $status -eq 0 || "$first" == "apogee models: nothing at "* ]]; then
        verdict="taken"
    else
        verdict="refused: ${first}"
    fi
    printf '%s\t%s\n' "$verdict" "$command" >>"$out"
    count=$((count + 1))
done <"$verdicts"
mv "$out" "$verdicts"

echo "recorded ${count} commands, the rename's list and its completion from $("$binary" version </dev/null | head -1) to ${fixtures#"${app_dir}/"}"
