#pragma once

#include <chrono>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#include "contracts/config.h"
#include "contracts/config_edit.h"

/// Where the collections live, by name (A4: moved out of `cli/embed` -- the
/// command line and the admin plane open them the same way), and what a write
/// into one asks before its first byte (CLI-M46).
namespace apogee::commands {

/// Where a collection's database lives: `~/.apogee/embeddings/<name>.db`.
///
/// One file per collection rather than one shared database. A collection is the
/// unit a user creates, searches and throws away, and `rm` on a single file is
/// a deletion story that cannot half-succeed.
[[nodiscard]] std::filesystem::path collection_path(std::string_view name);

/// Every collection currently on disk, sorted.
[[nodiscard]] std::vector<std::string> collection_names();

/// Whether `name` is a NEW collection (CLI-M46): no store at this spelling
/// (`collection_path`, so a case variant is the store on a folding
/// filesystem) and no reference to it, as written, in `config`
/// (`collection_references`; `config` may be null). A name that is not new
/// keeps working whatever its spelling.
[[nodiscard]] bool collection_is_new(std::string_view name, const harness::Config* config);

/// Why `name` may not be named as a collection by a new write or reference
/// (CLI-M46): the create rule (`collection_create_error`, folded against the
/// config's entries and the stores on disk) when the name is new; empty when
/// it is not new or the rule takes it. The adds that name a collection -- a
/// graph's members, an agent's collection -- ask it.
[[nodiscard]] std::string new_collection_error(std::string_view name,
                                               const harness::Config* config);

/// `new_collection_error` against the config at `config_path` as it is now --
/// one that will not load refers to nothing -- for a door adding a reference
/// to `name`: `agents create --collection` and the agent routes.
[[nodiscard]] std::string collection_reference_error(std::string_view name,
                                                     const std::filesystem::path& config_path);

/// A write into the collection `name` -- an ingest, a graph build, a captured
/// record -- as its plan reads it.
struct CollectionWrite {
    std::string name;
    /// The config as loaded; null when it would not load (a config-less home
    /// writes its store and says so, as before).
    const harness::Config* config = nullptr;
    /// The config file the registration edits; empty edits nothing.
    std::filesystem::path config_path{};
    /// The entry the registration would add.
    harness::EmbeddingConfig entry{};
    /// Whether the write sets `graph.enabled` after it: a collection's own
    /// graph build.
    bool enable_graph = false;
    /// A knowledge store, written by captures: its note's way out names
    /// `--db` and `knowledge.db`, never a re-ingest.
    bool records = false;
};

/// What a write may do, decided before its first byte, its clerk call, its
/// embedding or its extraction (CLI-M46).
struct CollectionWritePlan {
    /// Go: write, then run the edits below. Refuse: write nothing, say
    /// `words`. Note: write, say `words` once, and run no edit -- a store
    /// already on disk, or a name the config refers to, that the config
    /// cannot register.
    enum class Outcome { Go, Refuse, Note };
    Outcome outcome = Outcome::Go;
    /// The refusal or the note: one line, without the verb's prefix.
    std::string words{};
    /// No store and no config reference names it.
    bool is_new = false;
    /// The edits the write runs after it, only those today's conditions run
    /// -- the entry when there is none, `graph.enabled` when it is not
    /// already true -- each dry-run first on the config's text and kept
    /// nowhere, so none the dry run refuses is attempted.
    bool register_entry = false;
    bool enable_graph = false;

    [[nodiscard]] bool refused() const noexcept {
        return outcome == Outcome::Refuse;
    }
};

/// The plan for `write`: the read rule; for a new name the create rule; then
/// the dry run of the edits the write would run after it, on the config's text
/// read unlocked (a read before the lock answering a read-only question, 57f).
/// A refusal for a new name refuses the write, saying nothing was written; for
/// a name that is not new it is a note naming what the store cannot keep -- its
/// chunking, its graph's enable -- and the way out. The dry run is advisory:
/// `register_collection` runs the locked edits after the write and may still
/// fail.
[[nodiscard]] CollectionWritePlan plan_collection_write(const CollectionWrite& write);

/// What the registration after the write did.
struct CollectionRegistration {
    /// The entry was added by this call.
    bool registered = false;
    /// `graph.enabled` was set by this call.
    bool enabled = false;
    /// The edit that failed, the store left on disk, and why, in one line --
    /// beginning `could not register` or `could not set graph.enabled`;
    /// empty when every planned edit landed.
    std::string error{};

    [[nodiscard]] bool ok() const noexcept {
        return error.empty();
    }
};

/// Runs the edits `plan` planned for `write`, after the write, through the
/// one config editor, each under the config's lock (`lock_wait` bounds the
/// wait). A failure here -- a lock held past the wait, an edit raced in -- is
/// reported and never fatal: the store is already on disk (MILESTONES'
/// "Registration failure | Reported, never fatal").
[[nodiscard]] CollectionRegistration register_collection(
    const CollectionWrite& write, const CollectionWritePlan& plan,
    std::chrono::milliseconds lock_wait = harness::kConfigLockWait);

}  // namespace apogee::commands
