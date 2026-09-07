#pragma once

#include <cstdint>
#include <string>
#include <string_view>

#include "harness/config.h"

/// The one resolver for `models:` role pointers.
///
/// **This file exists because Ommi shipped the same logic twice and the copies
/// disagreed.** Its CLI and its HTTP admin plane each grew their own resolution
/// chain, and a request that ran on one backend from the terminal ran on
/// another over HTTP. The fix there was a single exported function both call;
/// Apogee starts from that fix instead of earning it again.
///
/// Apogee has no HTTP plane yet, and that is precisely why this lands **now**:
/// a resolver written as a private helper inside `complete.cpp` is a resolver
/// that gets copied the day `serve` needs one. Giving it a file makes the seam
/// visible enough that the copy looks wrong.
///
/// ## The chain
///
/// ```
/// explicit override  (-m, or a request's `model`)
///   > per-feature backend  (a collection's `backend:`, a graph's entry)
///     > the role pointer  (models.default_embedding / default_extraction)
///       > models.default
/// ```
///
/// With the role pointer unset the chain collapses to exactly
/// `override > models.default`, which is what every caller did before this
/// existed — so nothing that works today changes behaviour.
///
/// ## What it deliberately does NOT do
///
/// **It does not validate.** It returns a backend key; whether that key names a
/// configured backend is the caller's question, answered in the caller's idiom
/// — a fatal message on the CLI, a 400 over HTTP, a `Fail` row in `check`.
/// Folding validation in here would force one error shape on all three, and
/// that is the seam that made Ommi's two chains diverge in the first place.
namespace apogee::harness {

/// Which role a backend is being resolved for.
enum class ModelRole : std::uint8_t {
    /// Ordinary chat and completion. Falls straight through to `models.default`.
    Chat,
    /// Embedding work (RAG). Consults `models.default_embedding` first.
    Embedding,
    /// Structured extraction. Consults `models.default_extraction` first.
    Extraction,
};

/// The `models:` key a role reads, e.g. "default_embedding". `Chat` has none of
/// its own and answers "default".
[[nodiscard]] std::string_view to_string(ModelRole role) noexcept;

/// One resolution question.
///
/// A struct rather than four positional parameters because three of them are
/// strings and two are usually empty: `resolve_backend_key(cfg, role, "", key)`
/// is exactly the call that gets its arguments swapped.
struct RoleRequest {
    ModelRole role = ModelRole::Chat;

    /// An explicit choice by the caller: `-m` on the CLI, `model` on a request.
    /// Wins over everything.
    std::string_view override;

    /// A backend pinned by the feature being run — a collection's `backend:`,
    /// a named graph's entry. Empty when the feature pins none.
    std::string_view entry_backend;
};

/// Which rung of the chain produced an answer.
///
/// Reported rather than inferred, because "why did it pick that backend?" is
/// the question a user actually asks, and answering it by re-walking the chain
/// at the call site would be a second chain — the very thing this file exists
/// to prevent. `apogee models status` prints it.
enum class ResolvedFrom : std::uint8_t {
    /// Nothing was configured at any rung.
    Nothing,
    /// The caller's explicit `-m` or request `model`.
    Override,
    /// A backend pinned by the feature being run.
    EntryBackend,
    /// The role's own `models:` pointer.
    RolePointer,
    /// `models.default`.
    Default,
};

/// A resolved backend key and the rung it came from.
struct Resolution {
    std::string key;
    ResolvedFrom from = ResolvedFrom::Nothing;
};

/// Resolves `request`, reporting both the key and which rung answered.
///
/// Surrounding whitespace is trimmed at every rung: a config value of `" "` is
/// a typo, not a backend name, and treating it as one produces "no backend
/// named ' '" instead of falling through to the default the user expected.
[[nodiscard]] Resolution resolve_backend(const Config& config, const RoleRequest& request);

/// The key alone — the common case.
[[nodiscard]] std::string resolve_backend_key(const Config& config, const RoleRequest& request);

/// Shorthand for the overwhelmingly common case: chat, with an optional `-m`.
[[nodiscard]] std::string resolve_chat_backend(const Config& config, std::string_view override);

}  // namespace apogee::harness
