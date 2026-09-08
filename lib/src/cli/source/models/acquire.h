#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <string_view>

#include "models/sidecar.h"

/// Getting a model onto disk without ever leaving a half-one there.
///
/// **The whole file is one rule: copy → verify → commit.** Bytes stream into a
/// `.partial`; every check runs against that `.partial`; the rename to the real
/// name is the last operation and the only one that makes the file visible.
/// Any failure at any rung leaves nothing behind — no visible file, no sidecar,
/// not even the partial.
///
/// The reason to be this strict is that the failure it prevents is silent and
/// delayed. A half-downloaded model has a plausible name and a plausible size;
/// it fails much later, inside llama.cpp, as an error about the *model* rather
/// than about the download. Ommi's recorded version of this lesson is why
/// `models repair` exists there at all.
///
/// ## Verification protects integrity; it does not gate choice
///
/// Apogee has **no forbidden models** (SPEC → Background, divergence 4). This
/// code never refuses a ref because of what it is — only because the bytes that
/// arrived are not the bytes that were promised. A model whose architecture
/// Apogee has never heard of downloads and lands exactly like any other; an
/// advisory warning may be printed *before* the transfer starts, and the user's
/// answer is final.
///
/// ## Source-agnostic on purpose
///
/// Ommi had one source (Ollama) and its acquisition logic lived inside the
/// Ollama package. Apogee has two, and a second entry point bolted onto the
/// first is how the `.partial` discipline ends up implemented once carefully
/// and once carelessly. So the ladder lives here, takes bytes from a
/// `ByteSource` callback, and knows nothing about where they came from.
namespace apogee::models {

/// What a source promises about the bytes before they arrive.
///
/// Every field is optional because, with no allowlist, most of them usually
/// are: a published digest is the exception, not the rule.
struct SourcePromise {
    /// The ref the user named.
    std::string ref;
    /// "ollama" or "huggingface".
    std::string source;
    /// A URL or store path, for the record.
    std::string source_url;
    /// Hex sha256 the source published, without any "sha256:" prefix. Empty
    /// when the source publishes none.
    std::string digest;
    /// Declared size in bytes, or 0 when the source declares none.
    std::int64_t size = 0;
    /// Ollama's chat-template layer, when present. Recorded as a hint only.
    std::string template_hint;
};

/// Produces the bytes. Called repeatedly; returns false to signal failure,
/// and writes an explanation into `error`.
///
/// Injectable so every test in this area is hermetic: the Hugging Face path is
/// exercised without a network and the Ollama path without a store.
using ByteSource =
    std::function<bool(const std::function<bool(std::string_view)>& write, std::string& error)>;

/// Progress, for a surface that wants to show it. Bytes written so far, and the
/// expected total (0 when unknown). Optional.
using ProgressFn = std::function<void(std::int64_t written, std::int64_t total)>;

/// What happened.
struct AcquireResult {
    bool ok = false;
    /// Why not. Always set when `ok` is false.
    std::string error;
    /// Where the model landed. Empty unless `ok`.
    std::filesystem::path path;
    /// The record written beside it.
    Sidecar sidecar;
};

/// Streams `source` into `destination`, verifying before committing.
///
/// The ladder, in order:
///   1. stream into `<destination>.partial`
///   2. size, when the source declared one
///   3. sha256, when the source published one
///   4. GGUF header parse
///   5. rename `.partial` → `destination`, then write the sidecar
///
/// A failure at any rung removes the `.partial` and returns `ok == false`. The
/// sidecar is written only after the rename, so it can never describe a file
/// that is not there.
///
/// `destination` must not already exist; an existing file is an error rather
/// than an overwrite, because "pull" should never silently replace a model a
/// user is using.
[[nodiscard]] AcquireResult acquire(const std::filesystem::path& destination,
                                    const SourcePromise& promise, const ByteSource& source,
                                    const ProgressFn& progress = {});

/// Re-runs verification against an already-acquired model and its sidecar.
///
/// This is what `models repair` diagnoses with, and what makes the sidecar's
/// provenance/integrity split load-bearing: the comparison is against
/// `expected_digest()`/`expected_size()` — the file as it should be ON DISK —
/// not against what the source originally published.
[[nodiscard]] Verification reverify(const std::filesystem::path& model, const Sidecar& sidecar);

/// Whether `architecture` is one llama.cpp is known not to load.
///
/// **Advisory only.** It exists so a user can be warned before a multi-gigabyte
/// transfer, never to refuse one: the list is a snapshot of what was true when
/// it was written, and a policy that refuses on it would age into blocking
/// models that work.
[[nodiscard]] bool is_known_unrunnable(std::string_view architecture) noexcept;

}  // namespace apogee::models
