#include "backends/provider_table.h"

#include <algorithm>
#include <array>

namespace apogee::backends {

namespace {

// The rows' facts. Paths are home-relative and tested for existence only:
// this file is exempt from `cli.no_vendor_credentials`'s path patterns so it
// can NAME a vendor's files, and in exchange that check holds it to having no
// way to read one (tests/scripts/cmake/no_vendor_credentials.cmake).

constexpr std::array<std::string_view, 1> kVersion{"--version"};

// Claude Code keeps its account state in ~/.claude.json once it has run;
// the credential itself may be in the system's own store, never looked at.
constexpr std::array<std::string_view, 1> kClaudeEvidence{".claude.json"};

// Gemini CLI records the signed-in Google account here.
constexpr std::array<std::string_view, 1> kGeminiEvidence{".gemini/google_accounts.json"};

// Codex answers the question itself, offline, in about 65 ms: exit 0 when
// logged in (the 2026-10-03 spike).
constexpr std::array<std::string_view, 2> kCodexStatus{"login", "status"};

constexpr std::array<ProviderFacts, 7> kTable{{
    {"claude",
     harness::BackendType::ClaudeCli,
     "Claude CLI",
     "claude",
     kVersion,
     kClaudeEvidence,
     {}},
    {"codex", harness::BackendType::CodexCli, "Codex CLI", "codex", kVersion, {}, kCodexStatus},
    {"gemini",
     harness::BackendType::GeminiCli,
     "Gemini CLI",
     "gemini",
     kVersion,
     kGeminiEvidence,
     {}},
    // Ollama keeps no sign-in evidence offline that tells a signed-in machine
    // from one that only ran the server, so its credentials stay unknown.
    {"ollama", harness::BackendType::OllamaCli, "Ollama CLI", "ollama", kVersion, {}, {}},
    {"anthropic", harness::BackendType::Anthropic, "Anthropic API", "", {}, {}, {}},
    {"openai", harness::BackendType::OpenAI, "OpenAI API", "", {}, {}, {}},
    {"google", harness::BackendType::Google, "Google API", "", {}, {}, {}},
}};

}  // namespace

std::span<const ProviderFacts> provider_table() noexcept {
    return kTable;
}

const ProviderFacts* find_provider(std::string_view id) noexcept {
    const auto* found = std::ranges::find(kTable, id, &ProviderFacts::id);
    return found == kTable.end() ? nullptr : &*found;
}

const ProviderFacts* provider_for_type(harness::BackendType type) noexcept {
    const auto* found = std::ranges::find(kTable, type, &ProviderFacts::type);
    return found == kTable.end() ? nullptr : &*found;
}

}  // namespace apogee::backends
