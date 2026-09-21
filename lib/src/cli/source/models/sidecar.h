#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>

/// The record written beside every acquired model: where it came from, and
/// what it is.
///
/// **Provenance and integrity are separate facts, and this struct keeps them
/// apart on purpose.** What the source *claimed* (the ref, the URL, a published
/// digest, a size) and what is *actually on disk* are different things, and a
/// post-download transform makes them differ for real. Ommi learned this the
/// expensive way: it shipped the sidecar first, added a vision-stripping
/// transform later, and had to retrofit `FileSHA256`/`FileSize` beside the
/// original pin — with every previously written sidecar now ambiguous about
/// which digest it meant. Apogee writes both from the first version.
///
/// ## The digest is often absent, and that is not a failure
///
/// Ommi could always compare against a pinned digest because it chose the
/// models — a curated allowlist with a sha256 per entry. Apogee has no
/// allowlist by policy, so it downloads whatever the user names, and most
/// sources publish no digest for the file. The record therefore has to
/// distinguish three states that a single boolean would flatten into a lie:
///
///   * a digest was published and matched,
///   * a digest was published and did NOT match,
///   * no digest was published at all.
///
/// Only the second is a failure. Reporting the third as "unverified" in the
/// same breath as the second would train a user to ignore the word.
namespace apogee::models {

/// Which checks ran while a file was being accepted, and how each came out.
///
/// A report rather than a boolean, for the reason above.
struct Verification {
    /// The size was known in advance and the bytes on disk match it.
    bool size_checked = false;
    bool size_matched = false;

    /// A digest was published by the source and was compared.
    bool digest_checked = false;
    bool digest_matched = false;

    /// The GGUF header was read and understood.
    bool header_checked = false;
    bool header_parsed = false;

    /// Whether every check that RAN passed. A file with no published digest
    /// can still be `sound()`; it simply had fewer checks to pass.
    [[nodiscard]] bool sound() const noexcept {
        return (!size_checked || size_matched) && (!digest_checked || digest_matched) &&
               (!header_checked || header_parsed);
    }

    /// A short human summary, e.g. "size + header ok, no digest published".
    /// Never the bare word "verified": what was verified is the point.
    [[nodiscard]] std::string summary() const;
};

/// The provenance and integrity record for one acquired model.
struct Sidecar {
    // --- provenance: what the source said -----------------------------------

    /// The ref the user named, e.g. "llama3.2:3b" or "TheBloke/x:file.gguf".
    std::string ref;
    /// "ollama" or "huggingface".
    std::string source;
    /// A URL or store path the bytes came from.
    std::string source_url;
    /// The digest the source published, hex, without the "sha256:" prefix.
    /// Empty when the source published none — the common case.
    std::string published_digest;
    /// The size the source declared, or 0 when it declared none.
    std::int64_t published_size = 0;
    /// RFC3339 acquisition time.
    std::string pulled_at;

    // --- integrity: what is on disk -----------------------------------------

    /// The GGUF's basename.
    std::string file;
    /// sha256 of the file AS IT IS ON DISK, hex. Differs from
    /// `published_digest` after a transform; equal to it otherwise.
    std::string file_digest;
    /// Size of the file as it is on disk.
    std::int64_t file_size = 0;

    /// A post-download rewrite that changed the bytes, e.g. "vision-stripped".
    /// Empty means the file is the source bytes byte for byte.
    std::string transform;
    std::string transform_note;

    /// What was checked at acquisition time.
    Verification verification;

    /// Ollama's chat-template layer, when the source carried one.
    ///
    /// **A hint, recorded and never auto-applied.** Applying it would override
    /// the profile layer's resolution ladder from outside the ladder, which is
    /// exactly the kind of invisible precedence the profile item exists to end.
    std::string template_hint;

    /// The digest the file on disk should have: the post-transform value when
    /// there was a transform, else the published pin. Empty when neither is
    /// known, which means integrity cannot be rechecked later.
    [[nodiscard]] std::string expected_digest() const;

    /// The size the file on disk should have, or 0 when unknown.
    [[nodiscard]] std::int64_t expected_size() const;
};

/// Where a model's sidecar lives: the model path with a `.json` extension.
[[nodiscard]] std::filesystem::path sidecar_path_for(const std::filesystem::path& model);

/// Serialises `sidecar` as pretty JSON.
[[nodiscard]] std::string serialize(const Sidecar& sidecar);

/// Parses a sidecar. Returns nullopt for anything that is not one, rather than
/// throwing: a missing or corrupt sidecar means "provenance unknown", which is
/// a reportable state, not an error that should stop a listing.
[[nodiscard]] std::optional<Sidecar> parse_sidecar(std::string_view text);

/// Reads the sidecar beside `model`, or nullopt when there is none.
[[nodiscard]] std::optional<Sidecar> load_sidecar(const std::filesystem::path& model);

/// Writes `sidecar` beside its model, atomically.
///
/// Returns false on any failure. The caller's contract is that a sidecar is
/// written only AFTER its model is in place, so a sidecar never describes a
/// file that is not there.
[[nodiscard]] bool write_sidecar(const std::filesystem::path& model, const Sidecar& sidecar);

/// Streams `path` and returns its lowercase hex sha256, or "" if unreadable.
[[nodiscard]] std::string file_sha256(const std::filesystem::path& path);

}  // namespace apogee::models
