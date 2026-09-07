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
  # Role pointers. Each names an entry under `backends:` below, and all three
  # resolve through one shared chain:
  #     -m on the command line  >  a per-feature pin  >  the role pointer here
  #                             >  models.default
  # `apogee models status` prints which rung answered for each role.

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

  # ── Claude through the official CLI (subscription plan) ────────────────────
  # The subscription-auth path: Apogee spawns the `claude` binary you already
  # installed and logged into, as a long-lived child process. It never reads
  # your credentials -- not ~/.claude, not a keychain, not a session file.
  #
  # binary: resolved from PATH when unset.
  # mode:   subscription (default) uses whatever your CLI is logged into.
  #         bare passes --bare, which skips hook/MCP/CLAUDE.md discovery AND
  #         disables subscription auth -- the child then needs an API key.
  #         Right for CI; wrong on your own machine.
  #
  # Watch out for ANTHROPIC_API_KEY: if it is set in your environment, the CLI
  # prefers it over your subscription login, which quietly bills per token.
  # claude-sub:
  #   type: claude-cli
  #   mode: subscription
  #   # binary: /usr/local/bin/claude
  #   # model: claude-sonnet-5

  # ── Gemini through the official CLI (subscription plan) ────────────────────
  # The subscription-auth path for Google, beside the API-billing `google`
  # entry above -- both can live in this file at once, chosen per entry.
  # Apogee spawns the `gemini` binary you installed and signed into with your
  # Google account. It never reads your credentials -- not ~/.gemini, not a
  # keychain, not a session file.
  #
  # binary: resolved from PATH when unset.
  # There is no `mode` here: this CLI has no auth modes, so setting one is an
  # error rather than a no-op. For an API key, use the `google` entry above.
  #
  # Watch out for GEMINI_API_KEY: if it is set in your environment, the CLI
  # prefers it over your Google login, which quietly bills per token -- the
  # same trap ANTHROPIC_API_KEY sets for the claude-cli entry.
  #
  # Two flags are pinned and not configurable, because this CLI runs tools
  # while it answers: --approval-mode plan (read-only) and --skip-trust. The
  # second is not optional -- without it the CLI silently downgrades the
  # read-only pin to its default in an untrusted folder, and refuses to run
  # headless at all.
  # gem-sub:
  #   type: gemini-cli
  #   # binary: /usr/local/bin/gemini
  #   # model: gemini-3.5-flash

  # ── Local inference via llama.cpp ───────────────────────────────────────────
  # Runs in-process: no server and no listening socket. The model STAYS LOADED
  # between turns, which is what keeps a multi-turn chat warm -- each turn adds
  # only its new tokens to the KV cache instead of re-reading the conversation.
  # model_path is any GGUF you supply -- Apogee ships none and curates none, so
  # any model you point it at will run.
  #
  # context_size unset means "whatever this model was trained for", which is
  # usually what you want; set it to trade memory against conversation length.
  # idle_unload_seconds releases the weights after a quiet spell -- worth
  # setting if you switch between a local and a cloud backend in one session,
  # since the model is the largest thing the process holds.
  # local:
  #   type: llamacpp
  #   model_path: "${HOME}/.cache/llms/my-model.gguf"
  #   # context_size: 8192
  #   # idle_unload_seconds: 900

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
