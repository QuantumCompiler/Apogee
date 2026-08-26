#include <filesystem>
#include <fstream>
#include <string_view>

#include "harness/config.h"
#include "harness/config_edit.h"

// The starter config, embedded.
//
// C++20 has no #embed (that is C++23, and the standard is pinned at 20 for
// six-target reasons -- see CLAUDE.md -> Stack & environment), so the bytes
// live in a raw string literal. They are GENERATED from
// lib/src/cli/assets/config.yaml, and a test asserts the two are byte-identical
// so the shipped sample and `apogee config init` can never drift apart. If you
// edit one, regenerate the other -- CI fails otherwise.
//
// Ported from Ommi's template-drift test, which caught exactly this class of
// bug: a documented option that `config init` had quietly stopped writing.

namespace apogee::harness {
namespace {

constexpr std::string_view kConfigTemplate = R"APOGEE(# Apogee configuration.
#
# This file ships with everything commented out on purpose: a fresh install has
# no API keys and no models, and it must still load and pass `apogee check`.
# Local by default, cloud by choice -- uncomment only what you actually use.
#
# Edits made by `apogee config ...` preserve every comment in this file,
# including these. Hand-edit it freely; the tooling works around you.
#
# ${ENV_VAR} references are expanded when the file is read, and the literal
# text is what stays on disk -- so an api_key never has to appear here.

# Role pointers. Each names an entry under `backends:` below.
models:
  # The backend used when nothing else is specified.
  # default: claude

  # Used for embeddings (RAG). Apogee bundles no embedding model; point this at
  # a local GGUF you supply. With this unset, embedding falls back to
  # models.default -- which works for a local backend and not for a cloud one,
  # since cloud chat backends cannot embed.
  # default_embedding: embedder

  # Used for structured-extraction work. Unset means models.default.
  # default_extraction: extractor

# Optional search roots that pre-fill path prompts. Each is optional; an empty
# value simply means "no default". ${ENV_VAR} references are expanded.
paths:
  gguf_dir:        # .gguf model files
  hf_dir:          # HuggingFace SafeTensors directories
  mcp_dir:         # local MCP server scripts
  embeddings_dir:  # embedding database files

backends:

  # ── Anthropic (API billing plan) ────────────────────────────────────────────
  # Uses the Messages API directly with your own key. For the subscription
  # plan -- driving the `claude` CLI you are already logged into -- see the
  # claude-cli backend, which arrives after v0.1.0.
  # claude:
  #   type: anthropic
  #   api_key: "${ANTHROPIC_API_KEY}"
  #   model: claude-sonnet-5
  #   context_size: 200000
  #   max_tokens: 8192

  # ── OpenAI ──────────────────────────────────────────────────────────────────
  # gpt:
  #   type: openai
  #   api_key: "${OPENAI_API_KEY}"
  #   model: gpt-5
  #   context_size: 128000

  # ── Google ──────────────────────────────────────────────────────────────────
  # gemini:
  #   type: google
  #   api_key: "${GEMINI_API_KEY}"
  #   model: gemini-2.5-pro
  #   context_size: 1048576

  # ── Local inference via llama.cpp ───────────────────────────────────────────
  # Runs in-process: no server, no listening socket, nothing resident between
  # turns. model_path is any GGUF you supply -- Apogee ships none and curates
  # none, so any model you point it at will run.
  # local:
  #   type: llamacpp
  #   model_path: "${HOME}/.cache/llms/my-model.gguf"
  #   context_size: 8192

  # ── User-supplied embedding backend (vector RAG) ────────────────────────────
  # No embedding model ships with Apogee. To enable vector retrieval, point
  # model_path at a local GGUF -- a dedicated embedding model is best, though a
  # general instruct model works (mean-pooled, with variable quality). Then
  # uncomment default_embedding above. A collection must be ingested and
  # queried by the SAME model, so pick once or re-ingest after changing it.
  # embedder:
  #   type: llamacpp
  #   model_path: "${HOME}/.cache/llms/my-embedder.gguf"
  #   context_size: 2048

  # ── Mock ────────────────────────────────────────────────────────────────────
  # Answers from a canned script with no network and no model. Useful for
  # trying the CLI out, and for tests.
  # mock:
  #   type: mock

# How operational status output is displayed.
#   line    (default) one self-overwriting status line on a terminal
#   verbose every status message as a permanent line -- good for logs
#   quiet   no status output at all
# status_mode: line

# color: false turns off ANSI color everywhere. When unset, color is enabled on
# a terminal unless NO_COLOR is set or --no-color is passed.
# color: true
)APOGEE";

}  // namespace

std::string_view config_template() noexcept {
    return kConfigTemplate;
}

void save_config_template(const std::filesystem::path& path, bool force) {
    if (!force && std::filesystem::exists(path)) {
        throw ConfigEditError(path.string() +
                              ": config already exists; pass --force to overwrite it");
    }
    write_file_atomically(path, config_template());
}

}  // namespace apogee::harness
