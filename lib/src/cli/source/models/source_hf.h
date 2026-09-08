#pragma once

#include <memory>
#include <optional>
#include <string>
#include <string_view>

#include "backends/http_client.h"
#include "models/acquire.h"

/// Downloading a model file directly from Hugging Face.
///
/// **Entirely new — Ommi had no such path.** Ommi acquired models only through
/// Ollama, from a curated allowlist. Apogee's open-model policy means a user
/// can name any repository, so this is written from the API rather than ported.
///
/// ## The ref grammar
///
/// ```
/// owner/repo                 the repo's single .gguf, when it has exactly one
/// owner/repo:file.gguf       a named file in the repo
/// owner/repo@rev:file.gguf   a named file at a named revision
/// ```
///
/// A bare `owner/repo` is resolved by listing the repo and looking for one
/// `.gguf`. When a repo holds several — the usual case for a quantised upload,
/// where a dozen precisions sit side by side — this **refuses and lists them**
/// rather than guessing. Guessing a quantisation on the user's behalf spends
/// gigabytes of their bandwidth on a file they did not choose.
///
/// ## What it does not do
///
/// **No OAuth, no credential storage.** A gated repository needs a token, and
/// the only way to supply one is an optional config key or the conventional
/// `HF_TOKEN` environment variable, read at the point of use and never written
/// anywhere. That is the same line SPEC's *a vendor CLI is spawned, never
/// opened* draws for the vendor CLIs, applied to a plain HTTP source.
///
/// **No digest, usually.** Hugging Face publishes a size for most files and an
/// ETag that is sometimes a sha256 and sometimes not, so the verification
/// ladder here typically runs size and header checks and reports honestly that
/// no digest was published. That is the common case Apogee had to be built for.
namespace apogee::models {

/// A parsed Hugging Face ref.
struct HfRef {
    std::string owner;
    std::string repo;
    /// Empty when the ref named no file, which means "resolve it".
    std::string file;
    /// Empty means the default branch.
    std::string revision;

    [[nodiscard]] std::string repo_id() const {
        return owner + "/" + repo;
    }
};

/// Parses `ref`. Returns nullopt when it is not owner/repo shaped.
[[nodiscard]] std::optional<HfRef> parse_hf_ref(std::string_view ref);

/// The download URL for a fully-resolved ref.
[[nodiscard]] std::string hf_download_url(const HfRef& ref);

/// What listing a repository found.
struct HfListing {
    bool ok = false;
    std::string error;
    /// Every `.gguf` path in the repo, in the order the API returned them.
    std::vector<std::string> gguf_files;

    /// Whether the repo holds SafeTensors instead.
    ///
    /// Noticed, not downloaded. SPEC lists SafeTensors in scope and this item's
    /// residue still owes it; until then, knowing which kind of repository the
    /// user named is what turns "no .gguf" from a dead end into a next step.
    bool has_safetensors = false;
};

/// Lists a repository's GGUF files.
[[nodiscard]] HfListing list_gguf_files(backends::HttpClient& client, const HfRef& ref,
                                        std::string_view token,
                                        const harness::CancellationToken& cancellation);

/// Fills in `ref.file` when the ref named none.
///
/// Succeeds only when the repository holds exactly one GGUF. With several it
/// fails with a message naming them all, so the user picks; with none it says
/// so. Never guesses.
[[nodiscard]] bool resolve_file(backends::HttpClient& client, HfRef& ref, std::string_view token,
                                const harness::CancellationToken& cancellation, std::string& error);

/// A `ByteSource` that streams the file over `client`.
///
/// **No size is promised.** Hugging Face sends a Content-Length, but the
/// transport surfaces only status and body -- not response headers -- so there
/// is nothing to read it from without widening that seam, which is a change to
/// the HTTP client rather than to this item. The ladder therefore runs the
/// header check and reports "no digest published" honestly; a live 460 MB pull
/// confirms that reads as intended. Populating `promise.size` becomes possible
/// the day the transport exposes headers.
[[nodiscard]] ByteSource http_source(backends::HttpClient& client, const HfRef& ref,
                                     std::string_view token,
                                     const harness::CancellationToken& cancellation,
                                     SourcePromise& promise);

/// The token to use: the explicit value when non-empty, else `HF_TOKEN`, else
/// `HUGGING_FACE_HUB_TOKEN`, else empty. Read at the point of use and never
/// stored.
[[nodiscard]] std::string hf_token(std::string_view configured);

}  // namespace apogee::models
