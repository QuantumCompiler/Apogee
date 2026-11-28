#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

/// What may name a collection (CLI-M45): a collection is NAMED, never
/// pathed. Its store is a file the home builds, `embeddings/<name>.db`, so a
/// name joined to the folder must stay in it -- a `..` or an absolute path
/// would turn "search my notes" into a way to open any file on the machine,
/// and the store writes its schema into whatever it opens.
///
/// **One read rule, at every reader**: `--rag`, `auto_rag`, an agent's
/// collection, `serve --rag`, the rag and graph tools, `embed`, `graph`,
/// `knowledge` and their HTTP twins all ask it before a path is built. It
/// refuses only what can leave the folder, never a character set or a
/// length, so every store under `embeddings/` today keeps reading
/// (`my notes`, `a@b`, `a..b`, a 244-byte name). Which names may be
/// *created* is a narrower rule beside this one (CLI-M46).
///
/// **One create rule, for a new name** (CLI-M46): a store is written first
/// and registered in the config second, so a name the config cannot hold
/// left a store that forgot its chunking and its graph. The create rule
/// includes the read rule and adds what registration needs, so a name it
/// takes is one the config can hold: `embed ingest`, `graph build`, the
/// knowledge store and their HTTP twins ask it before the first byte, and
/// so do the adds that name a collection (a graph's members, an agent's
/// collection). A name already on disk or in the config is not new, and
/// keeps working whatever its spelling.
namespace apogee::harness {

struct Config;

/// Whether `name`, joined to its store folder, could reach outside it --
/// or is no name at all: empty, `..` as the whole name, a `/` or a `\` on
/// every platform, a NUL (which a C API reads as the end of the path), or
/// a root name, which `std::filesystem` would rebase the path onto (`D:x`
/// on Windows; a POSIX path has none, so a store named `a:b` reads there).
/// The one escape check: the dataset store's read rule asks it too
/// (`training::dataset_read_error`, CLI-M55).
[[nodiscard]] bool name_leaves_folder(std::string_view name);

/// Why `name` cannot name a collection to read, in the words every reader
/// says; empty when it can.
[[nodiscard]] std::string collection_read_error(std::string_view name);

/// `name` in single quotes as a refusal or a note says it, on one line: a
/// control character is written escaped (`\n`, `\t`, `\xNN`), so a store
/// named across a line break is still said in one line (CLI-M46).
[[nodiscard]] std::string quoted_collection_name(std::string_view name);

/// The longest new collection name, in bytes (CLI-M46): `<name>.db-journal`
/// beside it measured 255 bytes on APFS at 244, and Windows' path limit was
/// not measured, so new names keep a margin. Not in the read rule: a longer
/// store already on disk keeps reading.
inline constexpr std::size_t kNewCollectionNameMax = 200;

/// Why `name` cannot name a NEW collection, in the words every creator
/// says; empty when it can. In order: the read rule (its words); the
/// config's add rule (`yaml_plain_key`) less `/`, which the read rule
/// refuses -- letters, digits, `_`, `-` and `.`, first neither `-` nor `.`,
/// and not a null word; at most `kNewCollectionNameMax` bytes; no Windows
/// device name (`con`, `prn`, `aux`, `nul`, `com1`-`com9`, `lpt1`-`lpt9`,
/// case-folded, before any `.`) on any platform, since a store is a file a
/// Windows home could not hold; and no name that differs only in case from
/// one of `existing` (the config's entries and the stores on disk, as
/// spelled), which is named.
[[nodiscard]] std::string collection_create_error(std::string_view name,
                                                  const std::vector<std::string>& existing);

/// Every collection name `config` refers to, as written: its `embeddings:`
/// entries, `auto_rag`, `knowledge.db`, each agent's `collection` and each
/// named graph's members. A name here is not new (CLI-M46): a store under it
/// is written, and said, whatever its spelling.
[[nodiscard]] std::vector<std::string> collection_references(const Config& config);

}  // namespace apogee::harness
