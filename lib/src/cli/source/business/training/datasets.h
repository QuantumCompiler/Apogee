#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "contracts/types.h"
#include "training/synth.h"

/// The dataset store: `<APOGEE_HOME>/training/datasets/<name>.jsonl`, one
/// trainer-ready file per dataset, in the chat shape every driver reads:
///
///   {"messages":[{"role":"user","content":"…"},{"role":"assistant","content":"…"}]}
///
/// Datasets arrive three ways -- `prepare` converts a local file, `create`
/// scaffolds one (a documented template, nothing, or the user's own chat
/// sessions), `synth` distils one from a teacher -- and all three land here
/// through one writer, which is what lets the CLI and its admin twin produce
/// byte-identical files. The store is deliberately dumb: a directory of
/// files, the filesystem the source of truth, nothing cached.
namespace apogee::training {

struct DatasetInfo {
    std::string name;
    std::filesystem::path path;
    std::int64_t bytes = 0;
    int lines = 0;
    /// `chat`, `flat`, `eval`, `mixed`, `empty` or `unknown` -- what the
    /// lines look like, from their keys.
    std::string shape;
};

/// Letters, digits, `-`, `_` and `.`, not starting with a dot, at most 128
/// bytes: the character-set half of the create rule, and the floor `write`
/// keeps for every caller (CLI-M56). No reader asks it.
[[nodiscard]] bool valid_dataset_name(std::string_view name) noexcept;

/// **A dataset is named, not pathed** (CLI-M55): why `name` cannot name a
/// dataset to read, or empty when it can. One trailing `.jsonl` is dropped,
/// as `write` drops it, and what is left is asked the escape check every
/// collection reader asks (`harness::name_leaves_folder`, CLI-M45): the
/// empty name, `..`, a `/` or a `\` on every platform, a NUL, a root name.
/// Never a character set or a length, so every dataset `list` shows keeps
/// reading (`my set`, `café`, `.hidden`, `-h`, a 130-byte name). Every
/// reader asks it -- the store's `exists`, `info` and `remove`, `datasets
/// info` and `delete`, the HTTP GET and DELETE, and train's name branches --
/// so no reader joins a refused name to the folder.
[[nodiscard]] std::string dataset_read_error(std::string_view name);

/// Why `write` refuses `name`, or empty: its floor, `valid_dataset_name`
/// after one `.jsonl` is dropped, in today's words. Narrower than the read
/// rule and wider than the create rule, so a regime's work folder, which
/// `write` also fills with `<kit name>.jsonl` and reads by path, takes a kit
/// named `-x` as before (CLI-M56).
[[nodiscard]] std::string dataset_write_error(std::string_view name);

/// **A new dataset name is one every verb reads as a name** (CLI-M56): why
/// `name` cannot name a NEW dataset in `training/datasets/`, or empty when
/// it can. One trailing `.jsonl` is dropped, as `write` drops it, and what
/// is left is judged in order: the read rule (in its own words); the first
/// character neither `-` (every verb reads it as an option) nor `.`; the
/// character set and the 128-byte cap (`valid_dataset_name`); no Windows
/// device name (`harness::windows_device_name`, the list CLI-M46's
/// collection rule asks); no `.eval` ending, case-folded (the eval-suite
/// slot `train eval --suite` reads and `datasets prepare --as-eval`
/// fills); and no `.jsonl` left over, which would be another dataset.
/// Every other refusal is `'<name>' is not a dataset name: ` and the clause
/// the name breaks, and no clause says `already exists`, which HTTP answers
/// with a 409. Asked by every door that names a new dataset, before any other
/// work -- `create_dataset` (both create doors), synth's first check on the
/// CLI and over HTTP, and `prepare`'s derived name -- and by no reader, so
/// every dataset already on disk keeps reading whatever its name.
[[nodiscard]] std::string dataset_create_error(std::string_view name);

/// The shape of a JSONL file's lines, and their count.
[[nodiscard]] std::string dataset_shape(const std::filesystem::path& path, int& lines);

class DatasetStore {
public:
    explicit DatasetStore(std::filesystem::path dir);

    [[nodiscard]] const std::filesystem::path& dir() const noexcept {
        return dir_;
    }

    /// `<dir>/<name>.jsonl`; a name already ending in `.jsonl` is kept. The
    /// join itself checks nothing: a caller asks `dataset_read_error` (or
    /// `exists`, which asks it) first (CLI-M55).
    [[nodiscard]] std::filesystem::path path_for(std::string_view name) const;

    /// False for a name the read rule refuses, before any file is looked at.
    [[nodiscard]] bool exists(std::string_view name) const;

    /// Every dataset, sorted by name. A missing directory lists as empty.
    [[nodiscard]] std::vector<DatasetInfo> list() const;

    /// Nothing for a name the read rule refuses, as for an absent one.
    [[nodiscard]] std::optional<DatasetInfo> info(std::string_view name) const;

    /// Writes `lines` -- each one JSON object, a newline appended -- through a
    /// temp file and a rename. An existing dataset is refused without
    /// `force`. Returns the error text, or empty with `out` set to the path.
    [[nodiscard]] std::string write(std::string_view name, const std::vector<std::string>& lines,
                                    bool force, std::filesystem::path* out = nullptr) const;

    /// Removes the dataset. The error text -- the read rule's words for a
    /// name it refuses -- or empty.
    [[nodiscard]] std::string remove(std::string_view name) const;

private:
    std::filesystem::path dir_;
};

/// Where a scaffolded dataset's lines come from.
enum class CreateSource : std::uint8_t {
    /// Two documented example lines.
    Template,
    /// The user's persisted chat sessions, one example per exchange.
    Sessions,
    /// Zero lines.
    Empty,
};

[[nodiscard]] std::optional<CreateSource> create_source_from_string(std::string_view name) noexcept;
/// The names `create_source_from_string` accepts, for completion.
[[nodiscard]] std::span<const std::string_view> create_source_names() noexcept;

/// The formats `prepare_dataset.py --format` names -- the driver decides;
/// this is what completion offers, and a test holds it to the bundled driver.
[[nodiscard]] std::span<const std::string_view> prepare_formats() noexcept;
[[nodiscard]] std::string_view to_string(CreateSource source) noexcept;

/// One chat-format line, `role` before `content` -- the order the drivers
/// and `prepare_dataset.py` write, so every producer's bytes agree.
[[nodiscard]] std::string chat_line(std::string_view user, std::string_view assistant);

/// The template's two lines.
[[nodiscard]] std::vector<std::string> template_lines();

/// Synthesised examples as chat lines.
[[nodiscard]] std::vector<std::string> example_lines(const std::vector<SynthExample>& examples);

/// A session as the miner sees it: plain data, so this package never reads
/// the session store itself -- the surfaces load sessions and hand them
/// over, and both mine them the same way.
struct SessionView {
    std::string backend;
    /// RFC3339; the date is its first ten characters.
    std::string started_at;
    const std::vector<harness::ChatMessage>* messages = nullptr;
};

struct SessionFilter {
    /// Only sessions that ran on this backend; empty means any.
    std::string backend{};
    /// `YYYY-MM-DD`, inclusive both ends; empty means unbounded.
    std::string since{};
    std::string until{};
};

/// The filter's dates are well-formed. Empty when so.
[[nodiscard]] std::string validate_session_filter(const SessionFilter& filter);

struct MinedSessions {
    std::vector<std::string> lines;
    /// Sessions that contributed at least one line.
    int sessions = 0;
    /// Exchanges skipped because a side was empty.
    int skipped = 0;
};

/// One line per completed user↔assistant exchange with both sides
/// non-empty. Thinking and injected context are never in a session by
/// construction, so they cannot leak into a dataset; tool messages are
/// skipped, and an assistant turn that only called tools waits for the
/// answer that follows.
[[nodiscard]] MinedSessions mine_sessions(const std::vector<SessionView>& sessions,
                                          const SessionFilter& filter);

}  // namespace apogee::training
