#pragma once

#include <string>
#include <string_view>

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
namespace apogee::harness {

/// Whether `name`, joined to its store folder, could reach outside it --
/// or is no name at all: empty, `..` as the whole name, a `/` or a `\` on
/// every platform, a NUL (which a C API reads as the end of the path), or
/// a root name, which `std::filesystem` would rebase the path onto (`D:x`
/// on Windows; a POSIX path has none, so a store named `a:b` reads there).
[[nodiscard]] bool name_leaves_folder(std::string_view name);

/// Why `name` cannot name a collection to read, in the words every reader
/// says; empty when it can.
[[nodiscard]] std::string collection_read_error(std::string_view name);

}  // namespace apogee::harness
