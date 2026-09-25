#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "harness/types.h"
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

/// Letters, digits, `-`, `_` and `.`, not starting with a dot, at most 128.
[[nodiscard]] bool valid_dataset_name(std::string_view name) noexcept;

/// The shape of a JSONL file's lines, and their count.
[[nodiscard]] std::string dataset_shape(const std::filesystem::path& path, int& lines);

class DatasetStore {
public:
    explicit DatasetStore(std::filesystem::path dir);

    [[nodiscard]] const std::filesystem::path& dir() const noexcept {
        return dir_;
    }

    /// `<dir>/<name>.jsonl`; a name already ending in `.jsonl` is kept.
    [[nodiscard]] std::filesystem::path path_for(std::string_view name) const;

    [[nodiscard]] bool exists(std::string_view name) const;

    /// Every dataset, sorted by name. A missing directory lists as empty.
    [[nodiscard]] std::vector<DatasetInfo> list() const;

    [[nodiscard]] std::optional<DatasetInfo> info(std::string_view name) const;

    /// Writes `lines` -- each one JSON object, a newline appended -- through a
    /// temp file and a rename. An existing dataset is refused without
    /// `force`. Returns the error text, or empty with `out` set to the path.
    [[nodiscard]] std::string write(std::string_view name, const std::vector<std::string>& lines,
                                    bool force, std::filesystem::path* out = nullptr) const;

    /// Removes the dataset. The error text, or empty.
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
    std::string backend;
    /// `YYYY-MM-DD`, inclusive both ends; empty means unbounded.
    std::string since;
    std::string until;
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
