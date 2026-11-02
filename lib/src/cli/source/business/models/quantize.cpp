#include "models/quantize.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <system_error>
#include <utility>

#include "modelstore/gguf_inspect.h"

#if defined(APOGEE_ENABLE_LLAMA)
#include <llama.h>
#endif

namespace apogee::models {
namespace {

/// The curated set, with llama.cpp's enum value alongside each name.
///
/// A subset on purpose: llama.cpp defines dozens, most for research. These are
/// the ones people actually ship, and a short list is what lets a typo produce
/// a useful "did you mean" rather than a wall.
struct Entry {
    std::string_view name;
    std::string_view summary;
    int type;  // llama_ftype, kept as int so this table needs no llama.h
};

// The values are llama.cpp's LLAMA_FTYPE_MOSTLY_* constants. They are stable
// wire-visible numbers -- a GGUF records them -- so pinning them here rather
// than including llama.h keeps the table readable in a build without it. The
// static_asserts below fail the build if any of them ever moves.
constexpr std::array<Entry, 7> kTypes{{
    {.name = "Q2_K", .summary = "smallest, noticeable quality loss", .type = 10},
    {.name = "Q3_K_M", .summary = "very small, moderate quality loss", .type = 12},
    {.name = "Q4_K_M", .summary = "small, balanced -- the usual choice", .type = 15},
    {.name = "Q5_K_M", .summary = "medium, low quality loss", .type = 17},
    {.name = "Q6_K", .summary = "large, very low quality loss", .type = 18},
    {.name = "Q8_0", .summary = "largest quantized, minimal loss", .type = 7},
    {.name = "F16", .summary = "unquantized half precision", .type = 1},
}};

#if defined(APOGEE_ENABLE_LLAMA)
static_assert(static_cast<int>(LLAMA_FTYPE_MOSTLY_Q4_K_M) == 15,
              "llama.cpp's ftype numbering moved; the quantize table needs updating");
static_assert(static_cast<int>(LLAMA_FTYPE_MOSTLY_Q8_0) == 7,
              "llama.cpp's ftype numbering moved; the quantize table needs updating");
static_assert(static_cast<int>(LLAMA_FTYPE_MOSTLY_F16) == 1,
              "llama.cpp's ftype numbering moved; the quantize table needs updating");
#endif

[[nodiscard]] const Entry* find_type(std::string_view name) {
    const auto* const match = std::ranges::find_if(kTypes, [name](const Entry& entry) {
        return std::ranges::equal(entry.name, name, [](char a, char b) {
            return std::toupper(static_cast<unsigned char>(a)) ==
                   std::toupper(static_cast<unsigned char>(b));
        });
    });
    return match == kTypes.end() ? nullptr : &*match;
}

[[nodiscard]] std::string accepted_names() {
    std::string out;
    for (const Entry& entry : kTypes) {
        if (!out.empty()) {
            out += ", ";
        }
        out += entry.name;
    }
    return out;
}

#if defined(APOGEE_ENABLE_LLAMA)
/// The tensor count in one of llama.cpp's per-tensor quantize lines --
/// `[  12/ 291] blk.0.attn_q.weight - [...]` -- or nothing for any other
/// line. Lenient by design: a line it does not recognise only means no count
/// is said for it.
[[nodiscard]] std::optional<std::pair<std::size_t, std::size_t>> tensor_count(
    std::string_view line) {
    if (!line.starts_with('[')) {
        return std::nullopt;
    }
    const std::size_t slash = line.find('/');
    const std::size_t close = line.find(']');
    if (slash == std::string_view::npos || close == std::string_view::npos || slash > close) {
        return std::nullopt;
    }
    const auto number = [](std::string_view digits) -> std::optional<std::size_t> {
        while (!digits.empty() && digits.front() == ' ') {
            digits.remove_prefix(1);
        }
        if (digits.empty() || !std::ranges::all_of(digits, [](char c) {
                return std::isdigit(static_cast<unsigned char>(c)) != 0;
            })) {
            return std::nullopt;
        }
        return static_cast<std::size_t>(std::stoull(std::string{digits}));
    };
    const std::optional<std::size_t> done = number(line.substr(1, slash - 1));
    const std::optional<std::size_t> total = number(line.substr(slash + 1, close - slash - 1));
    if (!done.has_value() || !total.has_value()) {
        return std::nullopt;
    }
    return std::pair{*done, *total};
}

/// llama.cpp's log while one quantize runs, never printed (M3). `models
/// quantize` used to put its metadata dump and a line per tensor on the
/// terminal, where every other `models` verb reports in its own words.
/// Warnings and errors are kept, for a failure's message; the per-tensor
/// lines are the progress; the rest goes. The callback in place before --
/// llama.cpp's own, or the backend's -- is put back when this goes.
class QuantizeLog {
public:
    explicit QuantizeLog(const QuantizeProgress& progress) : progress_{progress} {
        llama_log_get(&previous_, &previous_data_);
        llama_log_set(&QuantizeLog::receive, this);
    }

    ~QuantizeLog() {
        llama_log_set(previous_, previous_data_);
    }

    QuantizeLog(const QuantizeLog&) = delete;
    QuantizeLog& operator=(const QuantizeLog&) = delete;
    QuantizeLog(QuantizeLog&&) = delete;
    QuantizeLog& operator=(QuantizeLog&&) = delete;

    /// What llama.cpp warned about or failed with, trimmed.
    [[nodiscard]] std::string said() {
        const std::scoped_lock lock{mutex_};
        std::string out = kept_;
        while (!out.empty() && (out.back() == '\n' || out.back() == ' ')) {
            out.pop_back();
        }
        return out;
    }

private:
    static void receive(ggml_log_level level, const char* text, void* self) {
        if (text != nullptr && self != nullptr) {
            static_cast<QuantizeLog*>(self)->add(level, text);
        }
    }

    void add(ggml_log_level level, std::string_view text) {
        if (level == GGML_LOG_LEVEL_INFO && progress_) {
            if (const auto count = tensor_count(text); count.has_value()) {
                progress_(count->first, count->second);
            }
        }
        const std::scoped_lock lock{mutex_};
        // A continuation belongs to whatever it continues.
        if (level != GGML_LOG_LEVEL_CONT) {
            keeping_ = level >= GGML_LOG_LEVEL_WARN;
        }
        if (!keeping_) {
            return;
        }
        kept_ += text;
        if (kept_.size() > kKept) {
            kept_.erase(0, kept_.size() - kKept);
        }
    }

    static constexpr std::size_t kKept = 2048;
    const QuantizeProgress& progress_;
    ggml_log_callback previous_ = nullptr;
    void* previous_data_ = nullptr;
    std::mutex mutex_;
    std::string kept_;
    bool keeping_ = false;
};
#endif

/// The bytes of a child's stderr kept for a failure's message.
constexpr std::size_t kChildTail = std::size_t{8} * 1024;

/// The last non-empty line of `tail`.
[[nodiscard]] std::string last_line_of(std::string_view tail) {
    while (!tail.empty() && (tail.back() == '\n' || tail.back() == '\r' || tail.back() == ' ')) {
        tail.remove_suffix(1);
    }
    const std::size_t at = tail.find_last_of('\n');
    return std::string{at == std::string_view::npos ? tail : tail.substr(at + 1)};
}

/// One line of the child's protocol read into `result`; true when it was the
/// result line.
bool read_protocol_line(std::string_view line, const QuantizeProgress& progress,
                        QuantizeResult& result) {
    const nlohmann::json event = nlohmann::json::parse(line, nullptr, false);
    if (!event.is_object()) {
        return false;  // not ours: a library's stray print, never acted on
    }
    if (const auto tensor = event.find("tensor"); tensor != event.end()) {
        const auto of = event.find("of");
        if (progress && tensor->is_number_unsigned() && of != event.end() &&
            of->is_number_unsigned()) {
            progress(tensor->get<std::size_t>(), of->get<std::size_t>());
        }
        return false;
    }
    const auto ok = event.find("ok");
    if (ok == event.end() || !ok->is_boolean()) {
        return false;
    }
    result.ok = ok->get<bool>();
    result.error = event.value("error", std::string{});
    result.input_bytes = event.value("input_bytes", std::int64_t{0});
    result.output_bytes = event.value("output_bytes", std::int64_t{0});
    if (!result.ok && result.error.empty()) {
        result.error = "the quantizer failed without saying why";
    }
    return true;
}

}  // namespace

platform::ChildCommand quantize_child_command(const std::filesystem::path& executable,
                                              const std::filesystem::path& input,
                                              const std::filesystem::path& output,
                                              std::string_view type) {
    platform::ChildCommand command;
    command.program = executable.string();
    command.arguments = {std::string{kQuantizeVerb}, input.string(), output.string(),
                         std::string{type}};
    return command;
}

int quantize_for_parent(const std::filesystem::path& input, const std::filesystem::path& output,
                        std::string_view type, std::ostream& out) {
    std::mutex writing;
    const auto say = [&out, &writing](const nlohmann::json& line) {
        const std::scoped_lock lock{writing};
        out << line.dump() << "\n";
        out.flush();
    };
    const QuantizeResult result =
        quantize(input, output, type, [&say](std::size_t done, std::size_t total) {
            say(nlohmann::json{{"tensor", done}, {"of", total}});
        });
    if (result.ok) {
        say(nlohmann::json{{"ok", true},
                           {"input_bytes", result.input_bytes},
                           {"output_bytes", result.output_bytes}});
        return 0;
    }
    say(nlohmann::json{{"ok", false}, {"error", result.error}});
    return 1;
}

QuantizeResult quantize_in_child(const platform::ChildCommand& command,
                                 const QuantizeProgress& progress,
                                 const harness::CancellationToken& cancellation) {
    QuantizeResult result;
    if (cancellation.stop_requested()) {
        result.stopped = true;
        result.error = "stopped";
        return result;
    }
    std::string error;
    const std::unique_ptr<platform::ChildProcess> child = platform::start_child(command, error);
    if (child == nullptr) {
        result.error = "could not start the quantizer (" + command.program + ")" +
                       (error.empty() ? std::string{} : ": " + error);
        return result;
    }
    child->close_stdin();

    std::string pending;  // a stdout line not yet ended
    std::string tail;     // the last of its stderr
    bool said = false;
    bool out_open = true;
    bool err_open = true;
    std::string piece;
    const auto stopped = [&child]() {
        child->terminate();
        (void)child->wait_for_exit(std::chrono::seconds{2});
        QuantizeResult out;
        out.stopped = true;
        out.error = "stopped";
        return out;
    };
    while (out_open || err_open) {
        if (cancellation.stop_requested()) {
            return stopped();
        }
        if (out_open) {
            switch (child->read_stdout(piece, std::chrono::milliseconds{50})) {
                case platform::ReadStatus::Data:
                    pending += piece;
                    for (std::size_t end = pending.find('\n'); end != std::string::npos;
                         end = pending.find('\n')) {
                        said = read_protocol_line(std::string_view{pending}.substr(0, end),
                                                  progress, result) ||
                               said;
                        pending.erase(0, end + 1);
                    }
                    break;
                case platform::ReadStatus::Timeout:
                    break;
                case platform::ReadStatus::Eof:
                case platform::ReadStatus::Error:
                    out_open = false;
                    break;
            }
        }
        if (err_open) {
            switch (child->read_stderr(piece, std::chrono::milliseconds{out_open ? 0 : 50})) {
                case platform::ReadStatus::Data:
                    tail += piece;
                    if (tail.size() > kChildTail) {
                        tail.erase(0, tail.size() - kChildTail);
                    }
                    break;
                case platform::ReadStatus::Timeout:
                    break;
                case platform::ReadStatus::Eof:
                case platform::ReadStatus::Error:
                    err_open = false;
                    break;
            }
        }
    }
    if (!pending.empty()) {
        said = read_protocol_line(pending, progress, result) || said;
    }
    // A Ctrl-C at the terminal reaches the child too, which dies of it: a
    // stop, whichever of the two was noticed first.
    if (cancellation.stop_requested()) {
        return stopped();
    }
    const std::optional<int> status = child->wait_for_exit(std::chrono::seconds{5});
    if (said && status.has_value() && (*status == 0) == result.ok) {
        return result;
    }
    QuantizeResult failed;
    const std::string last = last_line_of(tail);
    failed.error = said && !result.error.empty()
                       ? result.error
                       : "the quantizer ended without a result" +
                             (!status.has_value() ? std::string{" (it did not exit)"}
                              : *status >= 0      ? " (exit " + std::to_string(*status) + ")"
                                                  : std::string{" (killed by a signal)"}) +
                             (last.empty() ? std::string{} : ": " + last);
    return failed;
}

std::optional<std::string> canonical_quant_type(std::string_view name) {
    const Entry* entry = find_type(name);
    if (entry == nullptr) {
        return std::nullopt;
    }
    return std::string{entry->name};
}

std::vector<std::string> quant_type_names() {
    std::vector<std::string> names;
    for (QuantType& type : quant_types()) {
        names.push_back(std::move(type.name));
    }
    return names;
}

std::vector<QuantType> quant_types() {
    std::vector<QuantType> types;
    types.reserve(kTypes.size());
    for (const Entry& entry : kTypes) {
        types.push_back({.name = std::string{entry.name}, .summary = std::string{entry.summary}});
    }
    return types;
}

bool quantize_supported() noexcept {
#if defined(APOGEE_ENABLE_LLAMA)
    return true;
#else
    return false;
#endif
}

QuantizeResult quantize(const std::filesystem::path& input, const std::filesystem::path& output,
                        std::string_view type, const QuantizeProgress& progress) {
    QuantizeResult result;

    const Entry* entry = find_type(type);
    if (entry == nullptr) {
        result.error =
            "unknown quantization type '" + std::string{type} + "'. Accepted: " + accepted_names();
        return result;
    }

    std::error_code code;
    if (!std::filesystem::exists(input, code)) {
        result.error = "no such file: " + input.string();
        return result;
    }
    if (std::filesystem::exists(output, code)) {
        // The same rule acquisition follows: a quantize that silently replaced
        // a model someone was using would be the most expensive convenience.
        result.error = "a file already exists at " + output.string() + " -- delete it first";
        return result;
    }

    // The input has to actually be a GGUF, checked before llama.cpp is asked.
    // Its own failure for a non-GGUF is a log line and a non-zero return with
    // no explanation attached; ours names the file and the reason.
    const GgufInfo info = inspect_gguf(input);
    if (!info.parsed) {
        result.error = input.string() + " is not a readable GGUF -- " + info.parse_error;
        return result;
    }
    result.input_bytes = info.file_size;

    if (info.is_quantized()) {
        // llama.cpp refuses to requantize, and says so only after a couple of
        // hundred per-tensor log lines -- by which point the real reason is
        // long off the top of the screen. Saying it first, in one sentence,
        // costs a header read that already happened.
        result.error = input.filename().string() +
                       " is already quantized, and llama.cpp cannot requantize. Start from the "
                       "model's F16 or F32 release instead";
        return result;
    }

#if defined(APOGEE_ENABLE_LLAMA)
    llama_model_quantize_params params = llama_model_quantize_default_params();
    params.ftype = static_cast<llama_ftype>(entry->type);

    QuantizeLog log{progress};
    if (llama_model_quantize(input.string().c_str(), output.string().c_str(), &params) != 0) {
        // Which file and what was being attempted, then llama.cpp's own
        // specifics -- kept from its log rather than printed by it.
        result.error =
            "llama.cpp could not quantize " + input.string() + " to " + std::string{entry->name};
        if (const std::string said = log.said(); !said.empty()) {
            result.error += "\nllama.cpp said: " + said;
        }
        std::filesystem::remove(output, code);
        return result;
    }

    result.output_bytes = static_cast<std::int64_t>(std::filesystem::file_size(output, code));
    result.ok = true;
    return result;
#else
    // Present and refusing, rather than absent. A missing subcommand reads as
    // "Apogee cannot do this"; this reads as "this build cannot, and here is
    // the flag" -- which is the difference the refusal test pins.
    (void)entry;
    (void)progress;
    result.error =
        "this build cannot quantize: llama.cpp was not compiled in. Rebuild with "
        "-DAPOGEE_ENABLE_LLAMA=ON";
    return result;
#endif
}

}  // namespace apogee::models
