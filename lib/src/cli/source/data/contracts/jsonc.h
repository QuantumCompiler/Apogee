#pragma once

#include <nlohmann/json_fwd.hpp>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

/// JSONC -- JSON with `//` and `/* */` comments -- read with every value's
/// place in the text kept, and edited in place (28i).
///
/// The config file is JSONC because its comments are its documentation: the
/// starter template teaches through them, and a user's own notes must survive
/// every `apogee config ...` edit. So, exactly as the YAML editor before it,
/// nothing here ever re-serializes a document: `patch()` takes the text and
/// the value it should now hold, and splices only the members, items and
/// values that differ -- every other byte, comments and layout included,
/// stays where it was.
///
/// Strict JSON otherwise: no trailing commas, no single quotes, no duplicate
/// keys -- the file reads in any JSONC-aware editor, and in a strict parser
/// once its comments are stripped.
namespace apogee::harness::jsonc {

/// The text is not JSONC. The message names the line and column.
class JsoncError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

enum class Kind : std::uint8_t { Object, Array, String, Number, Boolean, Null };

/// One value, where it sits in the text.
struct Value {
    Kind kind = Kind::Null;
    /// Its first byte, and one past its last.
    std::size_t begin = 0;
    std::size_t end = 0;
    /// An object's member names, decoded, in the order written.
    std::vector<std::string> keys;
    /// Where each member's key string starts (its opening quote).
    std::vector<std::size_t> key_begins;
    /// An object's member values, or an array's items.
    std::vector<Value> children;
};

/// Whether `content` is JSONC rather than the legacy YAML: its first token,
/// past whitespace and a byte-order mark, opens an object or a comment.
/// Content decides, never a file name, so an edit's re-parse -- which has no
/// file -- reads the same way the load did.
[[nodiscard]] bool looks_like_jsonc(std::string_view content) noexcept;

/// The document's one value, or nullopt when the text holds nothing but
/// whitespace and comments. Throws JsoncError naming where it went wrong.
[[nodiscard]] std::optional<Value> parse_document(std::string_view text);

/// `parse_document`, a value required.
[[nodiscard]] Value parse(std::string_view text);

/// The value `node` holds, read from `text`.
[[nodiscard]] nlohmann::ordered_json to_json(const Value& node, std::string_view text);

/// The document as a value: `null` when it holds none.
[[nodiscard]] nlohmann::ordered_json parse_json(std::string_view text);

/// `value` in the house shape: two-space indentation under `indent` (the
/// prefix of the line it starts on), members in insertion order, an empty
/// object or array as `{}`/`[]`, an array of scalars on one line, lines
/// joined with `newline`. No trailing newline.
[[nodiscard]] std::string render(const nlohmann::ordered_json& value, std::string_view indent,
                                 std::string_view newline);

/// `text` edited to hold `target`, in place: a member missing from the text
/// is inserted after its preceding sibling (in `target`'s order) at that
/// sibling's indentation, a member `target` lacks is removed with its line,
/// an item appended or removed the same way, and a value that differs
/// replaced where it stands. Comments around a removed member stay -- they
/// may document its neighbours, and an orphaned comment is recoverable where
/// a deleted one is not. An insert and the matching removal are exact
/// inverses, byte for byte. Throws JsoncError when `text` does not parse.
[[nodiscard]] std::string patch(std::string_view text, const nlohmann::ordered_json& target);

/// Member `key` -- `lines`, whole lines each already indented: comments, then
/// the member, whose value may span several -- written into `object` of
/// `text` after its
/// member `after`, or before every member when nullopt. The separating comma
/// goes where JSON needs it: after the preceding member, or at byte
/// `comma_at` of the last line (where the inserted value ends, before a
/// comment that follows it) when a member comes after. How `config upgrade`
/// carries a template option in with the comments that teach it. An object
/// written on one line takes the member as a value instead, comments left
/// out, so the file stays valid in the shape it was written: `value` is the
/// member's value for that.
[[nodiscard]] std::string insert_lines(std::string_view text, const Value& object,
                                       std::optional<std::size_t> after, const std::string& key,
                                       std::vector<std::string> lines, std::size_t comma_at,
                                       const nlohmann::ordered_json& value);

/// The whitespace that opens the line `pos` is on.
[[nodiscard]] std::string indent_at(std::string_view text, std::size_t pos);

/// The line terminator `text` uses: `\r\n` when its first line ends so, else `\n`.
[[nodiscard]] std::string newline_of(std::string_view text);

}  // namespace apogee::harness::jsonc
