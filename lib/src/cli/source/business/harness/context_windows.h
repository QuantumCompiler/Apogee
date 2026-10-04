#pragma once

#include <cstdint>
#include <string_view>

/// The compiled model → context-window fallback table.
///
/// **Single owner.** Apogee tracks context usage for every backend, including
/// cloud ones — a divergence from Ommi, where the claude CLI managed its own
/// context and Ommi stayed out of the way. That means every backend needs a
/// window size, and a config entry that omits `context_size` needs a sensible
/// answer from somewhere.
///
/// That somewhere is here, and only here. The config engine, the cloud
/// backends, and the chat layer all consume this rather than growing their own
/// copies — three tables that disagree is how a context warning fires at the
/// wrong threshold on one surface and not another.
///
/// Provider-neutral data, like ModelBehavior: this header includes nothing from
/// the backends layer.
namespace apogee::harness {

/// The context window for `model`, or 0 when the model is unrecognised.
///
/// Matching is case-insensitive and prefix-based on the longest entry, so a
/// pinned id (`claude-sonnet-5-20260101`) resolves through its family prefix
/// (`claude-sonnet-5`) without the table needing a row per dated release.
///
/// **0 means "unknown", never "unlimited".** A caller must decide what to do
/// with an unknown window — usually skip context warnings rather than invent a
/// threshold, since a wrong threshold either nags constantly or never fires.
[[nodiscard]] std::int64_t context_window_for(std::string_view model) noexcept;

/// The window to use for a backend: its configured `context_size` when set and
/// positive, otherwise the table's answer for `model`.
///
/// This is the resolution order every caller should use, expressed once. An
/// explicit config value always wins — the user knows something we do not,
/// such as a self-hosted model served with a shortened window.
[[nodiscard]] std::int64_t resolve_context_window(std::int64_t configured,
                                                  std::string_view model) noexcept;

}  // namespace apogee::harness
