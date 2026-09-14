#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "harness/config.h"

/// The config engine's write path: line-oriented text surgery.
///
/// # Why not just re-serialize the struct
///
/// Because the comments ARE the documentation. A config file's commented-out
/// examples and field explanations are most of its value to the person editing
/// it, and every mainstream YAML library -- yaml-cpp included -- drops comments
/// on parse and reorders keys on emit. Load-modify-save would silently delete
/// the user's file contents. So Apogee never marshals a Config back to disk:
/// it locates the affected lines and splices them, leaving every other byte
/// exactly as it was. (Ommi reached the same conclusion and for the same
/// reason; toml++ would not have helped -- comment preservation there is an
/// open, unimplemented request.)
///
/// # The Core constraint this exists to satisfy
///
/// EVERY config mutation, from EVERY surface, forever, goes through these
/// helpers -- an edit made over HTTP must be byte-identical to the same edit
/// made from the CLI. That is not an aspiration to audit later; it is cheap
/// only because these functions exist before the surfaces that will call them.
///
/// # Shape
///
/// The transforms are PURE: text in, text out, no filesystem. That is a
/// deliberate divergence from Ommi, where each helper read, edited, and wrote
/// in one function. Separating them is what lets the golden-file suite run
/// with no filesystem at all, and it is what makes the two guarantees below
/// implementable:
///
///   * every edit is validated by RE-PARSING its own output before it lands;
///   * a failed edit leaves the file untouched (write-temp-then-rename).
///
/// Both live in edit_config_file(), which is the only thing here that writes.
namespace apogee::harness {

/// Raised when an edit cannot be applied: entry missing, name collision,
/// section malformed. Distinct from ConfigError (a *parse* failure) so a
/// caller can tell "your config is broken" from "that edit does not apply".
class ConfigEditError : public std::runtime_error {
public:
    explicit ConfigEditError(const std::string& message) : std::runtime_error(message) {}
};

// ---------------------------------------------------------------------------
// Pure transforms. Each takes the whole file's text and returns the whole
// file's new text. None of them touch the filesystem.
// ---------------------------------------------------------------------------

/// Names of the entries under `section:`, as written.
///
/// Only ACTIVE entries -- a commented-out example is not an entry, which is
/// what lets a template ship `# embedder:` without it counting as configured.
[[nodiscard]] std::vector<std::string> section_entry_names(std::string_view content,
                                                           std::string_view section);

/// An existing name that case-insensitively equals `candidate` without being
/// byte-identical to it, or nullopt.
///
/// This is Ommi's Viper-lowercasing lesson made explicit: there, adding
/// "Qwen3" beside an existing "qwen3" silently merged the two into one backend
/// at load. Apogee rejects the add and names the conflict instead.
[[nodiscard]] std::optional<std::string> fold_collision(const std::vector<std::string>& existing,
                                                        std::string_view candidate);

/// Adds a `backends:` entry, appended at the end of that section.
///
/// Creates the section when absent. Throws ConfigEditError on a case-fold
/// collision, or when the name already exists and `force` is false; with
/// `force` the existing entry is replaced in place of being duplicated.
[[nodiscard]] std::string append_backend(std::string_view content, std::string_view name,
                                         const BackendConfig& backend, bool force);

/// Removes a `backends:` entry: its key line, its field lines, and the single
/// blank separator line above it when there is one -- which makes this the
/// exact inverse of append_backend, so add-then-delete is byte-identical.
///
/// One documented exception: if the file did not end in a newline, appending
/// gave its final line one (nothing can follow an unterminated line), and
/// deleting cannot know to take it back. Every other byte round-trips.
///
/// A comment block sitting above the entry is deliberately LEFT BEHIND. It may
/// document the section rather than the entry, and an orphaned comment is
/// trivially recoverable where a deleted one is not.
///
/// Throws ConfigEditError when the entry is not found. Matching is
/// case-insensitive, and the entry is removed under the name the file
/// actually spells.
[[nodiscard]] std::string delete_backend(std::string_view content, std::string_view name);

/// Adds an `embeddings:` entry -- a RAG collection -- appended at the end of
/// that section, creating the section when absent.
///
/// The same shape and the same rules as append_backend, and the same
/// implementation underneath: the collision check, the placement above
/// trailing comments, and the blank separator that delete_embedding removes
/// again. `apogee embed ingest` calls this the first time it sees a
/// collection, which is the one config write in Apogee that a user did not
/// type `config` to get -- so it is held to the same byte-diff guarantee, and
/// a failure to register is reported and never fails the ingest.
[[nodiscard]] std::string append_embedding(std::string_view content, std::string_view name,
                                           const EmbeddingConfig& collection, bool force);

/// Removes an `embeddings:` entry. The exact inverse of append_embedding, with
/// the same documented exception for a file that did not end in a newline.
[[nodiscard]] std::string delete_embedding(std::string_view content, std::string_view name);

/// Field names accepted by set_models_role.
[[nodiscard]] std::vector<std::string_view> models_role_fields();

/// Sets `models.<field>` to `value`, replacing the existing line (keeping any
/// trailing comment on it) or inserting one, creating `models:` if needed.
///
/// `field` must be one of models_role_fields().
[[nodiscard]] std::string set_models_role(std::string_view content, std::string_view field,
                                          std::string_view value);

/// Appends an MCP server entry under `mcp_servers:` (creating the section),
/// fields alphabetical after the name and `enabled` always written, so a
/// later `set_mcp_server_enabled` replaces a line rather than inserting one.
/// Same collision and `force` rules as `append_backend`.
[[nodiscard]] std::string append_mcp_server(std::string_view content, std::string_view name,
                                            const McpServerConfig& server, bool force);

/// Removes the entry and its fields; the exact inverse of the append.
[[nodiscard]] std::string delete_mcp_server(std::string_view content, std::string_view name);

/// Sets `mcp_servers.<name>.enabled`, replacing the line in place (keeping
/// its trailing comment) or inserting one right after the name.
[[nodiscard]] std::string set_mcp_server_enabled(std::string_view content, std::string_view name,
                                                 bool enabled);

/// Appends an `agents:` entry (creating the section), fields alphabetical
/// after the name; a string or list is written only when set and a boolean
/// only when true, so an existing entry stays byte-identical across a
/// re-append of the same data -- Ommi's `formatAgentEntry` rule. `tools` is
/// always written: it is the permission model, and an entry that left it
/// implicit would read as "whatever the default is this release".
/// Same collision and `force` rules as `append_backend`.
[[nodiscard]] std::string append_agent(std::string_view content, std::string_view name,
                                       const AgentConfig& agent, bool force);

/// Removes the entry and its fields; the exact inverse of the append.
[[nodiscard]] std::string delete_agent(std::string_view content, std::string_view name);

/// Sets `permissions.<tool>` to `level` (`ask`, `allow`, or `deny`),
/// replacing the existing line (keeping any trailing comment) or inserting
/// one, creating `permissions:` if needed. The one path by which the
/// `[a]lways` answer, `config set-permission`, and the admin twin write.
///
/// `tool` is a tool name (`write_file`, `mcp__server__tool`): letters, digits
/// and underscores. Anything else is refused before the file is touched.
[[nodiscard]] std::string set_permission(std::string_view content, std::string_view tool,
                                         std::string_view level);

/// Tidies whitespace without touching content: strips trailing spaces, folds
/// runs of blank lines down to one, and ends the file with exactly one
/// newline.
///
/// Narrower than Ommi's FormatConfig, which also sorted the fields inside each
/// entry. Sorting was dropped on purpose: moving a field line moves it out
/// from under the comment that explains it, which is precisely the damage this
/// whole module exists to prevent.
[[nodiscard]] std::string format_config(std::string_view content);

// ---------------------------------------------------------------------------
// The file layer -- the only code here that writes.
// ---------------------------------------------------------------------------

/// Reads a file whole. Throws ConfigError when it cannot be read.
[[nodiscard]] std::string read_config_file(const std::filesystem::path& path);

/// Writes `content` to `path` via a temporary file in the same directory,
/// then renames it into place.
///
/// Same-directory matters: rename is only atomic within a filesystem, and a
/// temp file in /tmp can land on a different one. An interrupted write leaves
/// the original file intact rather than a truncated config.
/// With `private_mode`, the file is `0600` before it is renamed into place
/// (POSIX; a no-op where modes do not exist), so a secret is never on disk
/// under a permissive mode, not even between two syscalls.
void write_file_atomically(const std::filesystem::path& path, std::string_view content,
                           bool private_mode = false);

/// Reads `path`, applies `transform`, re-parses the result, and writes it
/// atomically.
///
/// The re-parse is the safety net that makes text surgery acceptable: if a
/// transform ever produces something the loader cannot read, the edit is
/// rejected and the file on disk is never touched. Nothing else in Apogee may
/// write a config file.
///
/// The file is also parsed BEFORE the transform, so "your config is already
/// broken" and "this edit would break it" are different messages.
template <typename Transform>
void edit_config_file(const std::filesystem::path& path, Transform&& transform) {
    const std::string original = read_config_file(path);
    (void)parse_config(original, path.string());

    std::string edited = std::forward<Transform>(transform)(std::string_view{original});

    // Validate before writing, never after. A rejected edit must leave the
    // user's file exactly as it was.
    (void)parse_config(edited, "<edited config>");

    write_file_atomically(path, edited);
}

}  // namespace apogee::harness
