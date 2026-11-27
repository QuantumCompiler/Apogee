#!/usr/bin/env bash
# refresh-store-commands-fixture.sh -- record which store commands a real
# `apogee` binary takes, deliberately (the GUI's M2; the binary named and the
# routes' verdicts since GUI M23).
#
#   refresh-store-commands-fixture.sh <apogee-binary>
#
# The convert and quantize sheets build commands whose flags the binary's
# `--help` names one by one; which combinations it takes is a rule between
# them that only the binary knows. StoreCommandsTests holds every command the
# sheets can build to the verdicts recorded here, so a sheet that builds one
# the binary refuses fails the app's suite, as do commands it never recorded.
#
# The binary is named, never defaulted: the dev channel's law keeps
# development off the production install, so this never reaches for
# ~/.local/bin/apogee. HOME and APOGEE_HOME are both a throwaway sandbox,
# `env -i` clears the rest, stdin is /dev/null, every run is denied the
# network (sandbox-exec), and nothing of ~/.apogee is touched.
#
# Writes, under tests/presentation/models/Fixtures/:
#   store-command-verdicts.txt  each command it already lists (a tab after the
#     verdict; a new command is added as a line `?<TAB><command>`), run again
#     and given the binary's verdict: `taken`, or `refused: <its first line>`.
#     Convert and quantize name a set that does not exist, so `nothing at …`
#     -- every flag rule passed -- is `taken`, and nothing is converted.
#   store-route-verdicts.txt    the same, over a store holding the sets the
#     CLI routes away from registering what a conversion makes (3a): a draft
#     head (Gemma4-E4B-Assistant) beside the base it reads, Wan's original
#     layout whole, and a Kokoro voice model -- each laid out as the CLI's own
#     tests lay them out (tests/support/draft_head_configs.h, wan_original.h,
#     voice_model.h), with a placeholder Python environment holding the files
#     the CLI reads to find mlx-video;
#   models-rename-list.json     `models list` of a sandbox holding a GGUF family
#     (qwen3-8b-Q4KM) and an MLX backend whose level is a mixed recipe
#     (GPT-OSS-20B-mixed_4_8), the rename's rows;
#   models-rename-complete.txt  `__complete models rename ""` there: the names
#     `models rename` takes.
#
# Every command runs in a throwaway home of its own, so a rename run changes
# nothing the next one reads.
set -euo pipefail
set -f # a command's words are never globs

[[ $# -eq 1 ]] || { echo "usage: refresh-store-commands-fixture.sh <apogee-binary>" >&2; exit 2; }
binary="$1"
[[ -x "$binary" ]] || { echo "refresh-store-commands-fixture.sh: no executable apogee at ${binary}" >&2; exit 1; }
app_dir="$(cd "$(dirname "$0")/.." && pwd)"
fixtures="${app_dir}/tests/presentation/models/Fixtures"
for file in store-command-verdicts.txt store-route-verdicts.txt; do
    [[ -f "${fixtures}/${file}" ]] || { echo "refresh-store-commands-fixture.sh: no tests/presentation/models/Fixtures/${file} to refresh" >&2; exit 1; }
done

work="$(mktemp -d "${TMPDIR:-/tmp}/apogee-store-commands.XXXXXX")"
trap 'rm -rf "$work"' EXIT
work="$(cd "$work" && pwd -P)"
# A run may write nowhere but the sandbox and the system's temporary and
# device paths, so a line naming a real path cannot touch it.
profile="(version 1)(allow default)(deny network-outbound (remote ip))(deny file-write* (require-not (require-any (subpath \"${work}\") (subpath \"/dev\") (subpath \"/private/var/folders\") (subpath \"/private/tmp\"))))"

# The binary in home $1, with the rest as its words: nothing of the person's
# environment and no network.
in_home() {
    local home="$1"
    shift
    env -i HOME="$home" APOGEE_HOME="$home/.apogee" PATH=/usr/bin:/bin \
        sandbox-exec -p "$profile" \
        "$binary" "$@" </dev/null
}

# The commands' sandbox: a seeded home, a GGUF family and an MLX model, each
# registered.
seed="${work}/seed/home"
mkdir -p "$seed/.apogee"
in_home "$seed" check --fix >/dev/null 2>&1 || true
gguf="${seed}/.apogee/models/qwen3-8b/gguf/6d0e12aa0c11"
mlx="${seed}/.apogee/models/GPT-OSS-20B/mlx/524b4fa3bc6a"
mkdir -p "$gguf" "$mlx"
printf 'GGUF' >"${gguf}/qwen3-8b-Q4_K_M.gguf"
printf '%s' '{"model_type":"gpt_oss","architectures":["GptOssForCausalLM"],"quantization":{"group_size":32,"bits":4,"mode":"mxfp4","model.layers.0.mlp.router":{"group_size":64,"bits":8,"mode":"affine"}}}' >"${mlx}/config.json"
printf 'x' >"${mlx}/model.safetensors"
in_home "$seed" config add-backend --type llamacpp --model-path "${gguf}/qwen3-8b-Q4_K_M.gguf" -- qwen3-8b-Q4KM >/dev/null
in_home "$seed" config add-backend --type mlx --model-path "$mlx" -- GPT-OSS-20B-mixed_4_8 >/dev/null

in_home "$seed" models list --output-format json >"${fixtures}/models-rename-list.json"
in_home "$seed" __complete models rename "" >"${fixtures}/models-rename-complete.txt"
sed -i '' -e "s#${seed}#/Users/someone#g" "${fixtures}/models-rename-list.json"

# The routes' sandbox: the sets the CLI refuses to register a conversion of.
routes="${work}/routes/home"
mkdir -p "$routes/.apogee"
in_home "$routes" check --fix >/dev/null 2>&1 || true
models="${routes}/.apogee/models"
zip_bytes() { # a PyTorch save's shape, $1 bytes: a zip's local header first, its end record last
    printf 'PK\003\004'
    head -c "$(($1 - 30))" /dev/zero | tr '\0' 'w'
    printf 'PK\005\006'
    head -c 22 /dev/zero
}
head_dir="${models}/Gemma4-E4B-Assistant/safetensors/b1c2d3e4f5a6"
base_dir="${models}/Gemma4-E4B/safetensors/9e1f2a3b4c5d"
wan_dir="${models}/Wan-AI--Wan2.2-TI2V-5B/safetensors/6ef9b00b8b83"
voice_dir="${models}/Kokoro-82M/voice/5f3a2c91d0e4"
mkdir -p "$head_dir" "$base_dir" "$wan_dir" "$voice_dir/voices"
printf '%s' '{"architectures": ["Gemma4AssistantForCausalLM"], "model_type": "gemma4_assistant", "backbone_hidden_size": 2560, "use_ordered_embeddings": true, "num_centroids": 2048, "text_config": {"model_type": "gemma4_text", "hidden_size": 256, "num_hidden_layers": 4}}' >"${head_dir}/config.json"
printf '%s' '{"architectures": ["Gemma4ForConditionalGeneration"], "model_type": "gemma4", "audio_config": {"model_type": "gemma4_audio", "hidden_size": 1536}, "vision_config": {"model_type": "gemma4_vision", "hidden_size": 768}, "text_config": {"model_type": "gemma4_text", "hidden_size": 2560, "max_position_embeddings": 131072}}' >"${base_dir}/config.json"
printf 'x' >"${head_dir}/model.safetensors"
printf 'x' >"${base_dir}/model.safetensors"
printf '%s' '{"_class_name": "WanModel", "_diffusers_version": "0.33.0", "dim": 3072, "eps": 1e-06, "ffn_dim": 14336, "freq_dim": 256, "in_dim": 48, "model_type": "ti2v", "num_heads": 24, "num_layers": 30, "out_dim": 48, "text_len": 512}' >"${wan_dir}/config.json"
printf '%s' '{"framework":"Pytorch","task":"text-video"}' >"${wan_dir}/configuration.json"
printf '%s' '{"metadata": {"total_size": 19999197184}, "weight_map": {}}' >"${wan_dir}/diffusion_pytorch_model.safetensors.index.json"
for file in diffusion_pytorch_model-00001-of-00003.safetensors diffusion_pytorch_model-00002-of-00003.safetensors \
    diffusion_pytorch_model-00003-of-00003.safetensors models_t5_umt5-xxl-enc-bf16.pth Wan2.2_VAE.pth; do
    printf 'fake bytes of %s, never weights\n' "$file" >"${wan_dir}/${file}"
done
printf '%s' '{"istftnet": {"upsample_rates": [10, 6]}, "plbert": {}, "vocab": {"a": 43}}' >"${voice_dir}/config.json"
zip_bytes 256 >"${voice_dir}/kokoro-v1_0.pth"
for voice in af_heart am_adam bf_emma; do zip_bytes 96 >"${voice_dir}/voices/${voice}.pt"; done
site="${routes}/.apogee/training/venv/lib/python3.12/site-packages"
mkdir -p "${site}/mlx_video/models/wan_2" "${site}/torch" "${routes}/.apogee/training/venv/bin"
printf '__version__ = "0.1.0"\n' >"${site}/mlx_video/__init__.py"
printf '' >"${site}/mlx_video/models/__init__.py"
printf '' >"${site}/mlx_video/models/wan_2/__init__.py"
printf '' >"${site}/mlx_video/models/wan_2/convert.py"
printf '__version__ = "2.6.0"\n' >"${site}/torch/__init__.py"

# Each command of `file`, in a copy of `home` of its own.
count=0
record() { # <file> <home>
    local file="${fixtures}/$1" from="$2" out="${work}/$1.new" copy answer status first verdict
    : >"$out"
    while IFS=$'\t' read -r _ command; do
        [[ -n "$command" ]] || continue
        copy="${work}/run-${count}/home"
        mkdir -p "$copy"
        cp -R "$from/." "$copy"
        read -r -a words <<<"$command"
        answer="$(in_home "$copy" "${words[@]}" 2>&1)" && status=0 || status=$?
        first="${answer%%$'\n'*}"
        first="${first//${copy}//Users/someone}"
        if [[ $status -eq 0 || "$first" == "apogee models: nothing at "* ]]; then
            verdict="taken"
        else
            verdict="refused: ${first}"
        fi
        printf '%s\t%s\n' "$verdict" "$command" >>"$out"
        count=$((count + 1))
        rm -rf "${work}/run-$((count - 1))"
    done <"$file"
    mv "$out" "$file"
}
record store-command-verdicts.txt "$seed"
record store-route-verdicts.txt "$routes"

echo "recorded ${count} commands, the rename's list and its completion from $(in_home "$seed" version | sed -n 1p) to ${fixtures#"${app_dir}/"}"
