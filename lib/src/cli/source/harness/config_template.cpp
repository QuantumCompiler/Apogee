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

  # Used for embeddings (RAG). With this unset, embedding falls back to
  # models.default. Which entries can embed is a property of the entry, not of
  # a list: an openai or google entry embeds with its vendor's embedding model
  # (see embedding_model on those entries), a llamacpp entry embeds with
  # whatever GGUF it holds, and anthropic cannot -- that vendor has no
  # embeddings endpoint. `apogee models status` says which rung answered.
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
  # embedding_model is what this entry uses when it EMBEDS rather than chats;
  # unset means text-embedding-3-small. One key serves both.
  # gpt:
  #   type: openai
  #   api_key: "${OPENAI_API_KEY}"
  #   model: gpt-5
  #   context_size: 128000
  #   # embedding_model: text-embedding-3-large

  # ── Google ──────────────────────────────────────────────────────────────────
  # embedding_model: unset means gemini-embedding-001.
  # gemini:
  #   type: google
  #   api_key: "${GEMINI_API_KEY}"
  #   model: gemini-2.5-pro
  #   context_size: 1048576
  #   # embedding_model: gemini-embedding-001

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

  # ── Local vision ────────────────────────────────────────────────────────────
  # A local model can read images when you also point it at that model's
  # multimodal projector -- a separate "mmproj" GGUF, usually published beside
  # the model itself. It is a field of its own rather than something Apogee
  # guesses: projectors have no reliable naming relationship to their model, and
  # the wrong one produces nonsense instead of an error.
  #
  # Needs a build with -DAPOGEE_ENABLE_LLAMA=ON. Without an mmproj_path the
  # entry is text-only and `--image` is refused with a message saying so.
  #
  # `apogee models info <backend>` tells you which of your files is which: a
  # projector reports as one rather than as a model.
  # vision:
  #   type: llamacpp
  #   model_path: "${HOME}/.cache/llms/SmolVLM-500M-Instruct-Q8_0.gguf"
  #   mmproj_path: "${HOME}/.cache/llms/mmproj-SmolVLM-500M-Instruct-Q8_0.gguf"

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

# ── Retrieval (RAG) ──────────────────────────────────────────────────────────
# Collections are made by `apogee embed ingest <name> <path>`; the first ingest
# of a new name registers it under `embeddings:` below, creating that section
# on first use. Registration is a convenience, not a requirement -- a
# collection works the moment its file exists. Every write here goes through
# the comment-preserving editor, so this commentary survives it.
#
# auto_rag names ONE collection to retrieve from on every turn without typing
# --rag. The flag still wins: `--rag other` for a single run, `--rag ""` to
# switch it off for a single run. Read each turn, so an edit here takes effect
# on the next question rather than the next session. The status line always
# says when context was injected this way.
# auto_rag: notes

# Each collection can also say HOW it is searched. Three retrievers exist:
#   lexical  BM25 full-text; needs no model and no network (the floor)
#   vector   cosine over embeddings; needs an embedding backend, and the
#            collection must have been ingested with that same model
#   hybrid   both, fused by rank (never by score); explicit only -- auto
#            never picks it
# With nothing set, `auto` picks vector when the collection's vectors match
# the embedding backend that would answer, else lexical -- and never spends
# money on your behalf to build vectors: ingesting a whole collection through
# a paid embedder (openai, google) takes `--retriever vector` or a `retriever:
# vector` pin, while answering a question against one already built is one
# small call and is allowed. A local embedder costs nothing either way.
#
# rerank names a backend that reorders retrieved chunks with one generation
# call and drops the ones that only share words with the question. Every
# failure of the judge falls back to the raw order and says so.
#
# embeddings:
#   notes:
#     chunk_size: 512        # codepoints per chunk; the default for re-ingests
#     chunk_overlap: 64      # codepoints shared between neighbouring chunks
#     description: "Meeting notes"
#     # backend: embedder    # which entry embeds this collection (default: models.default_embedding)
#     # retriever: auto      # lexical | vector | hybrid | auto -- checked by `apogee check`
#     # rerank: off          # a backend name, or off
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
