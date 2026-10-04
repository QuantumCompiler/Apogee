#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "models/acquire.h"

/// Reading a model out of the user's own Ollama store.
///
/// **This reads a model store, not credentials.** The distinction matters
/// because SPEC's *a vendor CLI is spawned, never opened* forbids touching a
/// vendor's auth; `~/.ollama/models/` holds neither tokens nor sessions, only
/// content-addressed blobs the user already downloaded. `cli.no_vendor_credentials`
/// deliberately does not list this directory, and says so where the rule lives.
///
/// ## The layout
///
/// ```
/// <root>/manifests/<registry>/<namespace>/<name>/<tag>   plain-JSON manifest
/// <root>/blobs/sha256-<hex>                              content-addressed layers
/// ```
///
/// A manifest lists layers by media type. The
/// `application/vnd.ollama.image.model` layer's digest names a blob that **is a
/// raw GGUF** — first four bytes `GGUF` — so getting the model is a file copy,
/// not a conversion. `<root>` is `$OLLAMA_MODELS` when set, else
/// `~/.ollama/models`.
///
/// ## ⚠ Blobs are shared, so nothing here ever deletes one
///
/// Sibling models reference identical blobs — license and template layers
/// routinely, weights occasionally. Deleting a blob because "its" model is
/// going away silently corrupts every other model that referenced it. Ommi
/// recorded this rule and Apogee keeps it: **the only mutation of the Ollama
/// store is `ollama rm`**, the vendor's own reference-counting GC. This module
/// is read-only by construction — it has no delete path to misuse.
namespace apogee::models {

/// One model found in the store.
struct OllamaEntry {
    /// The ref as the user would name it, e.g. "llama3.2:3b".
    std::string ref;
    /// Absolute path of the blob holding the GGUF.
    std::filesystem::path blob;
    /// Size of that blob, from the manifest.
    std::int64_t size = 0;
    /// The layer digest, hex without the "sha256:" prefix. Ollama's blobs are
    /// content-addressed, so this IS a published digest — one of the few
    /// sources that gives Apogee one.
    std::string digest;
    /// The chat-template layer's text, when the manifest carries one. A hint,
    /// recorded and never auto-applied.
    std::string template_hint;

    /// The multimodal projector's blob, when this model has one.
    ///
    /// **Ollama ships the projector as its OWN layer** —
    /// `application/vnd.ollama.image.projector` — rather than fused into the
    /// model blob. That was worth checking rather than assuming: the plan
    /// inherited from Ommi was to *extract* a projector out of a combined file,
    /// and the manifests say there is nothing to extract. `llava` and
    /// `moondream` both carry the two layers side by side.
    ///
    /// Empty for a text-only model.
    std::filesystem::path projector_blob;
    std::int64_t projector_size = 0;
    std::string projector_digest;

    [[nodiscard]] bool has_projector() const noexcept {
        return !projector_blob.empty();
    }
};

/// The store root: `$OLLAMA_MODELS`, else `~/.ollama/models`.
[[nodiscard]] std::filesystem::path ollama_store_root();

/// Splits "name:tag" into its parts. A ref with no tag gets Ollama's implicit
/// "latest".
[[nodiscard]] std::pair<std::string, std::string> split_ref(std::string_view ref);

/// The manifest path for `ref` under `root`.
///
/// A namespaced ref ("user/model:tag") lives under its own namespace directory;
/// a bare one under "library", which is where the official models are.
[[nodiscard]] std::filesystem::path manifest_path(const std::filesystem::path& root,
                                                  std::string_view ref);

/// Parses a manifest's JSON into an entry, resolving its blob path under
/// `root`. Returns nullopt when the JSON is not a manifest or carries no model
/// layer — which is the case for a **cloud** model, whose `layers` array is
/// empty because the weights live on Ollama's servers.
[[nodiscard]] std::optional<OllamaEntry> parse_manifest(const std::filesystem::path& root,
                                                        std::string_view ref,
                                                        std::string_view manifest_json);

/// Whether the store has a manifest for `ref` at all.
///
/// Separate from `find_in_store` because the two answers need different
/// messages: a ref with no manifest has not been pulled, while a ref WITH a
/// manifest but no model layer is a **cloud** model whose weights live on
/// Ollama's servers. Telling the second user to run `ollama pull` sends them to
/// re-fetch something they already have.
[[nodiscard]] bool store_has_manifest(const std::filesystem::path& root, std::string_view ref);

/// Finds `ref` in the store at `root`.
///
/// Returns nullopt when there is no such manifest, or when it names no local
/// weights. The caller turns that into a message; this reports, it does not
/// refuse.
[[nodiscard]] std::optional<OllamaEntry> find_in_store(const std::filesystem::path& root,
                                                       std::string_view ref);

/// Every model with local weights in the store, for a listing.
[[nodiscard]] std::vector<OllamaEntry> list_store(const std::filesystem::path& root);

/// A `ByteSource` that copies `entry`'s blob.
[[nodiscard]] ByteSource blob_source(const OllamaEntry& entry);

/// The promise an Ollama entry makes: a real digest and a real size, which is
/// better than most sources manage.
[[nodiscard]] SourcePromise promise_for(const OllamaEntry& entry);

/// A `ByteSource` and promise for the entry's **projector**, when it has one.
///
/// A second acquisition rather than a bolt-on to the first: it is a separate
/// file with its own digest and its own size, so it goes through the same
/// copy → verify → commit ladder and gets its own provenance record. Sharing
/// one call would mean one sidecar describing two files.
[[nodiscard]] ByteSource projector_source(const OllamaEntry& entry);
[[nodiscard]] SourcePromise projector_promise_for(const OllamaEntry& entry);

}  // namespace apogee::models
