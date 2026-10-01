#include "agentloop/attachments.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <fstream>
#include <iterator>
#include <map>
#include <memory>
#include <system_error>
#include <utility>

#include "agent/fetch_url.h"
#include "agent/readable.h"
#include "agentloop/media.h"
#include "embedstore/chunk.h"
#include "embedstore/ingest.h"
#include "harness/errors.h"
#include "models/sha256.h"
#include "platform/child_process.h"

namespace apogee::agentloop {
namespace {

/// Chunks embedded per call, so a long file reports progress and can be
/// cancelled between calls.
constexpr std::size_t kEmbedBatch = 32;

/// The host a local HTML file's links are resolved against, then rewritten
/// back to `file://`: the reader resolves links against a web address only.
constexpr std::string_view kLocalHost = "file.invalid";

[[nodiscard]] std::string lowercase(std::string text) {
    std::ranges::transform(text, text.begin(),
                           [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return text;
}

[[nodiscard]] std::string read_bytes(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return {};
    }
    return std::string{std::istreambuf_iterator<char>{in}, std::istreambuf_iterator<char>{}};
}

/// Runs `program` with `arguments` to its end and returns its stdout, or
/// nullopt when it could not run or failed.
[[nodiscard]] std::optional<std::string> run_capture(const std::string& program,
                                                     std::vector<std::string> arguments) {
    if (!platform::supports_child_processes() || platform::find_on_path(program).empty()) {
        return std::nullopt;
    }
    platform::ChildCommand command;
    command.program = program;
    command.arguments = std::move(arguments);
    std::string error;
    const std::unique_ptr<platform::ChildProcess> child = platform::start_child(command, error);
    if (child == nullptr) {
        return std::nullopt;
    }
    child->close_stdin();
    std::string out;
    std::string piece;
    for (;;) {
        const platform::ReadStatus status =
            child->read_stdout(piece, std::chrono::milliseconds{200});
        if (status == platform::ReadStatus::Data) {
            out += piece;
        } else if (status != platform::ReadStatus::Timeout) {
            break;
        }
    }
    const std::optional<int> status = child->wait_for_exit(std::chrono::seconds{10});
    if (!status.has_value() || *status != 0) {
        return std::nullopt;
    }
    return out;
}

/// Whether any component of `relative` is hidden.
[[nodiscard]] bool hidden(const std::filesystem::path& relative) {
    return std::ranges::any_of(relative, [](const std::filesystem::path& part) {
        const std::string name = part.string();
        return name.size() > 1 && name.front() == '.' && name != "..";
    });
}

/// `name` as the model cites it: relative to `working_directory` when inside
/// it, else absolute.
[[nodiscard]] std::string cited_name(const std::filesystem::path& path,
                                     const std::filesystem::path& working_directory) {
    const std::filesystem::path relative = path.lexically_relative(working_directory);
    if (!relative.empty() && *relative.begin() != "..") {
        return relative.generic_string();
    }
    return path.generic_string();
}

/// The NUL-separated entries of `listing`.
[[nodiscard]] std::vector<std::filesystem::path> nul_separated(std::string_view listing) {
    std::vector<std::filesystem::path> out;
    std::size_t start = 0;
    while (start < listing.size()) {
        const std::size_t end = std::min(listing.find('\0', start), listing.size());
        if (end > start) {
            out.emplace_back(std::string{listing.substr(start, end - start)});
        }
        start = end + 1;
    }
    return out;
}

/// What git lists under `folder` -- tracked, and untracked but not ignored --
/// or nullopt when `folder` is not in a repository or git is not there.
[[nodiscard]] std::optional<std::vector<std::filesystem::path>> git_files(
    const std::filesystem::path& folder) {
    const std::optional<std::string> inside =
        run_capture("git", {"-C", folder.string(), "rev-parse", "--is-inside-work-tree"});
    if (!inside.has_value() || !inside->starts_with("true")) {
        return std::nullopt;
    }
    const std::optional<std::string> listed = run_capture(
        "git",
        {"-C", folder.string(), "ls-files", "-z", "--cached", "--others", "--exclude-standard"});
    if (!listed.has_value()) {
        return std::nullopt;
    }
    return nul_separated(*listed);
}

/// Every file under `folder` by walking it, hidden entries left out.
[[nodiscard]] std::vector<std::filesystem::path> walked_files(const std::filesystem::path& folder) {
    std::vector<std::filesystem::path> out;
    std::error_code code;
    std::filesystem::recursive_directory_iterator walk{
        folder, std::filesystem::directory_options::skip_permission_denied, code};
    for (; !code && walk != std::filesystem::recursive_directory_iterator{}; walk.increment(code)) {
        const std::filesystem::path relative = walk->path().lexically_relative(folder);
        if (!hidden(relative)) {
            if (walk->is_regular_file(code)) {
                out.push_back(relative);
            }
        } else if (walk->is_directory(code)) {
            walk.disable_recursion_pending();
        }
    }
    return out;
}

/// Every file under `folder`, relative to it: what `git ls-files` lists inside
/// a repository, else a walk -- hidden entries left out either way.
[[nodiscard]] std::vector<std::filesystem::path> files_under(const std::filesystem::path& folder) {
    // Not `value_or`: that would walk the whole folder -- a repository's
    // build output and all -- even when git has answered.
    std::optional<std::vector<std::filesystem::path>> listed = git_files(folder);
    std::vector<std::filesystem::path> out =
        listed.has_value() ? std::move(*listed) : walked_files(folder);
    std::erase_if(out, [](const std::filesystem::path& relative) { return hidden(relative); });
    std::ranges::sort(out);
    const auto [first, last] = std::ranges::unique(out);
    out.erase(first, last);
    return out;
}

[[nodiscard]] bool glob_match(std::string_view pattern, std::string_view text);

/// `**/rest`: zero folders, or any number of whole ones, then `rest`.
[[nodiscard]] bool match_folders(std::string_view rest, std::string_view text) {
    if (glob_match(rest, text)) {
        return true;
    }
    for (std::size_t index = 0; index < text.size(); ++index) {
        if (text[index] == '/' && glob_match(rest, text.substr(index + 1))) {
            return true;
        }
    }
    return false;
}

/// `*rest` (`across` false: within one name) or `**rest` (`across` true).
[[nodiscard]] bool match_star(std::string_view rest, std::string_view text, bool across) {
    for (std::size_t index = 0; index <= text.size(); ++index) {
        if (glob_match(rest, text.substr(index))) {
            return true;
        }
        if (!across && index < text.size() && text[index] == '/') {
            return false;
        }
    }
    return false;
}

/// Whether `text` matches `pattern`: `*` and `?` within one name, `**`
/// across folders. NOLINTNEXTLINE(misc-no-recursion): bounded by the pattern.
[[nodiscard]] bool glob_match(std::string_view pattern, std::string_view text) {
    if (pattern.empty()) {
        return text.empty();
    }
    if (pattern.starts_with("**/")) {
        return match_folders(pattern.substr(3), text);
    }
    if (pattern.starts_with('*')) {
        const bool across = pattern.starts_with("**");
        return match_star(pattern.substr(across ? 2 : 1), text, across);
    }
    if (text.empty()) {
        return false;
    }
    const bool one = pattern.front() == '?' ? text.front() != '/' : pattern.front() == text.front();
    return one && glob_match(pattern.substr(1), text.substr(1));
}

/// A refused file type's reason, or empty.
[[nodiscard]] std::string refused_type(const std::string& extension) {
    static const std::map<std::string, std::string, std::less<>> kRefused{
        {".doc", "a Word document"},
        {".docx", "a Word document"},
        {".odt", "a word-processor document"},
        {".rtf", "a rich-text document"},
        {".xls", "an Excel spreadsheet"},
        {".xlsx", "an Excel spreadsheet"},
        {".ods", "a spreadsheet"},
        {".ppt", "a PowerPoint deck"},
        {".pptx", "a PowerPoint deck"},
        {".odp", "a presentation"},
    };
    if (const auto it = kRefused.find(extension); it != kRefused.end()) {
        return it->second + ": Office formats are not read yet";
    }
    // A name to read the extension from: ".jpg" alone is a hidden file with
    // none.
    if (const std::optional<harness::Medium> medium =
            medium_of(std::filesystem::path{"file" + extension});
        medium.has_value()) {
        return std::string{harness::to_string(*medium)} +
               ", which a model reads rather than as text";
    }
    return {};
}

/// An HTML file's main content as Markdown, its links absolute.
[[nodiscard]] AttachmentText read_html(const std::filesystem::path& path) {
    AttachmentText out;
    const std::string bytes = read_bytes(path);
    if (bytes.empty()) {
        out.reason = "empty or unreadable";
        return out;
    }
    if (embedstore::looks_binary(bytes)) {
        out.reason = "looks binary";
        return out;
    }
    // Resolved against the file's own address, then written back as file://.
    agent::HttpUrl base;
    base.scheme = "https";
    base.host = std::string{kLocalHost};
    base.target = std::filesystem::absolute(path).generic_string();
    if (!base.target.starts_with('/')) {
        base.target.insert(0, "/");
    }
    const agent::ReadablePage page = agent::extract_readable(agent::as_utf8(bytes, {}), base);
    std::string text = page.text;
    const std::string prefix = "https://" + std::string{kLocalHost} + "/";
    for (std::size_t at = text.find(prefix); at != std::string::npos; at = text.find(prefix, at)) {
        text.replace(at, prefix.size(), "file:///");
    }
    if (!page.title.empty() && !text.starts_with("# ")) {
        text = "# " + page.title + "\n\n" + text;
    }
    if (text.find_first_not_of(" \t\r\n") == std::string::npos) {
        out.reason = "no readable text";
        return out;
    }
    out.text = std::move(text);
    out.reader = "html";
    return out;
}

/// A chunk's metadata, parsed; an empty object when it has none.
[[nodiscard]] nlohmann::json metadata_of(const embedstore::Chunk& chunk) {
    const nlohmann::json parsed = nlohmann::json::parse(chunk.metadata, nullptr, false);
    return parsed.is_object() ? parsed : nlohmann::json::object();
}

/// `a` and `b` (1-based, inclusive) as a range: `41` or `41–42`.
[[nodiscard]] std::string range(std::int64_t first, std::int64_t last) {
    return last > first ? std::to_string(first) + "–" + std::to_string(last)
                        : std::to_string(first);
}

/// The label for chunks `first` to `last` of one file.
[[nodiscard]] std::string label_of(const nlohmann::json& first, const nlohmann::json& last) {
    const std::string file = first.value("file", std::string{"an attachment"});
    if (first.contains("times") && last.contains("times")) {
        const double from = first["times"][0].get<double>();
        const double to = last["times"][1].get<double>();
        return file + " " + clock_time(from) + (to > from ? "–" + clock_time(to) : "");
    }
    if (first.contains("pages") && last.contains("pages")) {
        const std::int64_t from = first["pages"][0].get<std::int64_t>();
        const std::int64_t to = last["pages"][1].get<std::int64_t>();
        return file + (to > from ? " pp. " : " p. ") + range(from, to);
    }
    if (first.contains("lines") && last.contains("lines")) {
        return file + ":" +
               range(first["lines"][0].get<std::int64_t>(), last["lines"][1].get<std::int64_t>());
    }
    return file;
}

/// `next` appended to `text`, whose last chunk ended at byte `end`: the
/// overlap they share is not repeated.
void append_after(std::string& text, std::int64_t end, const embedstore::Chunk& next,
                  const nlohmann::json& meta) {
    const std::int64_t begin = meta.value("begin", std::int64_t{0});
    const std::int64_t overlap = end > begin ? end - begin : 0;
    if (static_cast<std::size_t>(overlap) < next.text.size()) {
        text += next.text.substr(static_cast<std::size_t>(overlap));
    }
}

/// A file's chunks, and each one's metadata: its name, its content key, its
/// byte offsets, and its page or line range.
struct ChunkedFile {
    std::vector<std::string> texts;
    std::vector<std::string> metadata;
};

[[nodiscard]] ChunkedFile chunk_file(const AttachmentText& read, std::string_view name,
                                     std::string_view sha256) {
    ChunkedFile out;
    const bool paged = read.reader == "pdftotext";
    const embedstore::PositionIndex lines{read.text, '\n'};
    const embedstore::PositionIndex pages{read.text, '\f'};
    for (const embedstore::TextSpan& span : embedstore::chunk_spans(read.text)) {
        std::string text = read.text.substr(span.begin, span.end - span.begin);
        // A chunk that ends on a line's or a page's break belongs to it.
        const std::size_t last = span.end > span.begin ? span.end - 1 : span.begin;
        nlohmann::json meta{{"file", name},
                            {"sha256", sha256},
                            {"reader", read.reader},
                            {"begin", span.begin},
                            {"end", span.end}};
        if (paged) {
            meta["pages"] = {pages.number_at(span.begin), pages.number_at(last)};
            std::ranges::replace(text, '\f', '\n');
        } else if (read.reader == "text") {
            meta["lines"] = {lines.number_at(span.begin), lines.number_at(last)};
        } else if (const std::optional<TimeSpan> times = time_span(read.text, span.begin, span.end);
                   times.has_value()) {
            // A transcript or a timeline: dated by the stamps its lines open
            // with, so it is cited, and found, by its moment (26e).
            meta["times"] = {times->start, times->end};
        }
        out.texts.push_back(std::move(text));
        out.metadata.push_back(meta.dump());
    }
    return out;
}

}  // namespace

bool FoundFiles::large() const noexcept {
    return files.size() > kLargeAttachmentFiles || bytes > kLargeAttachmentBytes;
}

FoundFiles find_attachment_files(std::string_view spec,
                                 const std::filesystem::path& working_directory) {
    FoundFiles out;
    std::error_code code;
    const auto add = [&](const std::filesystem::path& path) {
        const std::uintmax_t size = std::filesystem::file_size(path, code);
        out.files.push_back(FoundFile{.path = path,
                                      .name = cited_name(path, working_directory),
                                      .bytes = code ? 0 : static_cast<std::uint64_t>(size)});
        out.bytes += out.files.back().bytes;
    };

    if (spec.find_first_of("*?") != std::string_view::npos) {
        // The folder before the first wildcard is walked; the rest matched.
        const std::size_t wildcard = spec.find_first_of("*?");
        const std::size_t slash = spec.rfind('/', wildcard);
        const std::string folder_part =
            slash == std::string_view::npos ? std::string{"."} : std::string{spec.substr(0, slash)};
        const std::string_view pattern =
            slash == std::string_view::npos ? spec : spec.substr(slash + 1);
        std::filesystem::path folder{folder_part};
        if (folder.is_relative()) {
            folder = working_directory / folder;
        }
        folder = folder.lexically_normal();
        if (!std::filesystem::is_directory(folder, code)) {
            out.error = "no folder at " + folder_part;
            return out;
        }
        for (const std::filesystem::path& relative : files_under(folder)) {
            if (glob_match(pattern, relative.generic_string())) {
                add(folder / relative);
            }
        }
        if (out.files.empty()) {
            out.error = "nothing matches " + std::string{spec};
        }
        return out;
    }

    std::filesystem::path path{std::string{spec}};
    if (path.is_relative()) {
        path = working_directory / path;
    }
    path = path.lexically_normal();
    if (std::filesystem::is_regular_file(path, code)) {
        add(path);  // named explicitly, so hidden or ignored alike
        return out;
    }
    if (std::filesystem::is_directory(path, code)) {
        for (const std::filesystem::path& relative : files_under(path)) {
            add(path / relative);
        }
        if (out.files.empty()) {
            out.error = std::string{spec} + " holds no files";
        }
        return out;
    }
    out.error = "no file or folder at " + std::string{spec};
    return out;
}

AttachmentText read_attachment_text(const std::filesystem::path& path) {
    AttachmentText out;
    const std::string extension = lowercase(path.extension().string());
    if (std::string refused = refused_type(extension); !refused.empty()) {
        out.reason = std::move(refused);
        return out;
    }
    if (extension == ".html" || extension == ".htm" || extension == ".xhtml") {
        return read_html(path);
    }
    std::string reason;
    if (!embedstore::read_as_text(path, out.text, reason)) {
        out.reason = reason;
        out.text.clear();
        return out;
    }
    out.reader = embedstore::is_pdf(path) ? "pdftotext" : "text";
    return out;
}

std::string attachment_source(std::string_view sha256) {
    return "sha256:" + std::string{sha256};
}

std::vector<AttachmentExcerpt> attachment_excerpts(const std::vector<embedstore::SearchHit>& hits) {
    // Grouped by file, in reading order, so neighbours can merge.
    std::map<std::string, std::vector<const embedstore::SearchHit*>> by_source;
    for (const embedstore::SearchHit& hit : hits) {
        by_source[hit.chunk.source].push_back(&hit);
    }
    std::vector<AttachmentExcerpt> out;
    for (auto& [source, group] : by_source) {
        std::ranges::sort(group, {},
                          [](const embedstore::SearchHit* hit) { return hit->chunk.ordinal; });
        std::size_t index = 0;
        while (index < group.size()) {
            const nlohmann::json first = metadata_of(group[index]->chunk);
            AttachmentExcerpt excerpt;
            excerpt.source = source;
            excerpt.text = group[index]->chunk.text;
            excerpt.score = group[index]->score;
            std::int64_t end = first.value("end", std::int64_t{0});
            nlohmann::json last = first;
            std::size_t next = index + 1;
            while (next < group.size() &&
                   group[next]->chunk.ordinal == group[next - 1]->chunk.ordinal + 1) {
                last = metadata_of(group[next]->chunk);
                append_after(excerpt.text, end, group[next]->chunk, last);
                end = last.value("end", end);
                excerpt.score = std::max(excerpt.score, group[next]->score);
                ++next;
            }
            excerpt.label = label_of(first, last);
            out.push_back(std::move(excerpt));
            index = next;
        }
    }
    std::ranges::stable_sort(out, std::greater<>{}, &AttachmentExcerpt::score);
    return out;
}

std::string render_attachment_excerpts(const std::vector<AttachmentExcerpt>& excerpts) {
    if (excerpts.empty()) {
        return {};
    }
    std::string out =
        "The following excerpts are from files the user attached to this conversation. Use them "
        "if they help, and when one informs your answer, cite it by the label in its header.\n";
    for (const AttachmentExcerpt& excerpt : excerpts) {
        out += "\n--- " + excerpt.label + " ---\n" + excerpt.text + "\n";
    }
    return out;
}

namespace {

/// Whether `token` -- trimmed of `:` and `_` at its ends -- is a code name;
/// `called` when `()` follows it.
[[nodiscard]] bool is_code_name(std::string_view token, bool called) {
    const auto alnum = [](char c) { return std::isalnum(static_cast<unsigned char>(c)) != 0; };
    bool snake = false;
    bool camel = false;
    for (std::size_t index = 1; index < token.size(); ++index) {
        snake = snake || (token[index] == '_' && index + 1 < token.size() &&
                          alnum(token[index - 1]) && alnum(token[index + 1]));
        camel = camel || (std::islower(static_cast<unsigned char>(token[index - 1])) != 0 &&
                          std::isupper(static_cast<unsigned char>(token[index])) != 0);
    }
    return snake || camel || called || token.find("::") != std::string_view::npos;
}

}  // namespace

std::vector<std::string> code_names_in(std::string_view question) {
    std::vector<std::string> out;
    const auto part_of_name = [](char c) {
        return std::isalnum(static_cast<unsigned char>(c)) != 0 || c == '_' || c == ':';
    };
    std::size_t at = 0;
    while (at < question.size()) {
        while (at < question.size() && !part_of_name(question[at])) {
            ++at;
        }
        const std::size_t start = at;
        while (at < question.size() && part_of_name(question[at])) {
            ++at;
        }
        std::string_view token = question.substr(start, at - start);
        const std::size_t first = token.find_first_not_of(":_");
        const std::size_t last = token.find_last_not_of(":_");
        if (first == std::string_view::npos) {
            continue;
        }
        token = token.substr(first, last - first + 1);
        if (is_code_name(token, question.substr(at).starts_with("()"))) {
            out.emplace_back(token);
        }
    }
    return out;
}

std::vector<embedstore::SearchHit> hits_at(const embedstore::Store& store,
                                           const std::vector<double>& moments,
                                           const std::set<std::string>& exclude, double score) {
    std::vector<embedstore::SearchHit> out;
    if (moments.empty()) {
        return out;
    }
    for (const embedstore::Chunk& chunk : store.chunks_with_metadata()) {
        if (exclude.contains(chunk.source)) {
            continue;
        }
        const nlohmann::json meta = metadata_of(chunk);
        if (!meta.contains("times")) {
            continue;
        }
        const double from = meta["times"][0].get<double>();
        const double to = meta["times"][1].get<double>();
        if (std::ranges::any_of(moments, [&](double at) { return at >= from && at <= to; })) {
            out.push_back(embedstore::SearchHit{.chunk = chunk, .score = score});
        }
    }
    return out;
}

std::string render_inline_attachment(std::string_view name, std::string_view text) {
    return "--- attached file: " + std::string{name} + " ---\n" + std::string{text} +
           (text.ends_with('\n') ? "" : "\n") + "--- end of " + std::string{name} + " ---\n";
}

AttachmentIndex::AttachmentIndex(std::filesystem::path store_path, std::filesystem::path others,
                                 std::optional<Embedder> embedder, MediaReader media)
    : store_path_{std::move(store_path)},
      others_{std::move(others)},
      embedder_{std::move(embedder)},
      media_{std::move(media)} {}

std::string AttachmentIndex::model() const {
    return embedder_.has_value() ? embedder_->model : std::string{};
}

bool AttachmentIndex::holds(std::string_view sha256) const {
    std::error_code code;
    if (!std::filesystem::exists(store_path_, code)) {
        return false;
    }
    const embedstore::Store store{store_path_};
    return !store.chunks_by_source(attachment_source(sha256)).empty();
}

AttachmentIndex::Added AttachmentIndex::copy_from(const std::filesystem::path& other_path,
                                                  const FoundFile& file, std::string_view sha256) {
    Added out;
    const embedstore::Store other{other_path};
    const std::vector<embedstore::Chunk> chunks = other.chunks_by_source(attachment_source(sha256));
    if (chunks.empty()) {
        return out;
    }
    // The same vector space, or lexical beside lexical: a copy from another
    // model's space would mix two in one index.
    std::vector<std::vector<float>> vectors;
    vectors.reserve(chunks.size());
    for (const embedstore::Chunk& chunk : chunks) {
        vectors.push_back(other.chunk_vector(chunk.id));
    }
    const bool vectorised = embedder_.has_value();
    const bool complete = std::ranges::none_of(
        vectors, [](const std::vector<float>& vector) { return vector.empty(); });
    const bool same_space =
        vectorised ? complete && other.embedding_model().model == model() : vectors.front().empty();
    if (!same_space) {
        return out;
    }
    std::vector<std::string> texts;
    std::vector<std::string> metadata;
    std::string reader;
    for (const embedstore::Chunk& chunk : chunks) {
        nlohmann::json meta = metadata_of(chunk);
        meta["file"] = file.name;  // cited by this chat's name for it
        reader = meta.value("reader", reader);
        texts.push_back(chunk.text);
        metadata.push_back(meta.dump());
    }
    embedstore::Store store{store_path_};
    if (vectorised && !store.embedding_model().recorded()) {
        store.set_embedding_model(model(), static_cast<std::int64_t>(vectors.front().size()));
    }
    store.replace_source(attachment_source(sha256), texts,
                         vectorised ? vectors : std::vector<std::vector<float>>{}, metadata);
    out.file = logger::AttachedFile{.name = file.name,
                                    .path = file.path.string(),
                                    .sha256 = std::string{sha256},
                                    .reader = reader,
                                    .bytes = file.bytes};
    out.copied = true;
    out.chunks = static_cast<std::int64_t>(chunks.size());
    out.vectorised = vectorised;
    return out;
}

AttachmentIndex::Added AttachmentIndex::copy_from_others(const FoundFile& file,
                                                         std::string_view sha256) {
    std::error_code code;
    if (others_.empty() || !std::filesystem::is_directory(others_, code)) {
        return {};
    }
    for (const auto& entry : std::filesystem::directory_iterator{others_, code}) {
        if (entry.path().extension() != ".db" ||
            std::filesystem::equivalent(entry.path(), store_path_, code)) {
            continue;
        }
        try {
            if (Added copied = copy_from(entry.path(), file, sha256); copied.file.has_value()) {
                return copied;
            }
        } catch (const std::exception&) {
            // Another chat's damaged index is no reason to fail this one.
        }
    }
    return {};
}

AttachmentIndex::Added AttachmentIndex::held(const FoundFile& file, std::string_view sha256) const {
    Added out;
    const embedstore::Store store{store_path_};
    const std::vector<embedstore::Chunk> chunks = store.chunks_by_source(attachment_source(sha256));
    out.file = logger::AttachedFile{.name = file.name,
                                    .path = file.path.string(),
                                    .sha256 = std::string{sha256},
                                    .reader = metadata_of(chunks.front()).value("reader", ""),
                                    .bytes = file.bytes};
    out.already = true;
    out.chunks = static_cast<std::int64_t>(chunks.size());
    out.vectorised = !store.chunk_vector(chunks.front().id).empty();
    return out;
}

std::optional<std::vector<std::vector<float>>> AttachmentIndex::embed_chunks(
    const std::vector<std::string>& texts, std::string_view name,
    const harness::CancellationToken& cancellation, const Progress& progress,
    std::string& note) const {
    std::vector<std::vector<float>> vectors;
    if (!embedder_.has_value()) {
        return vectors;
    }
    const embedstore::Store store{store_path_};
    if (const embedstore::Store::EmbeddingBinding binding = store.embedding_model();
        binding.recorded() && binding.model != model()) {
        note = std::string{name} + ": this chat's index holds vectors from " + binding.model +
               ", so it is searched by its words only";
        return vectors;
    }
    try {
        for (std::size_t start = 0; start < texts.size(); start += kEmbedBatch) {
            cancellation.throw_if_cancelled();
            const std::size_t end = std::min(texts.size(), start + kEmbedBatch);
            const std::vector<std::string> batch{texts.begin() + static_cast<std::ptrdiff_t>(start),
                                                 texts.begin() + static_cast<std::ptrdiff_t>(end)};
            std::vector<std::vector<float>> embedded = embedder_->embed(batch, cancellation);
            if (embedded.size() != batch.size()) {
                throw std::runtime_error("the embedder returned " +
                                         std::to_string(embedded.size()) + " vectors for " +
                                         std::to_string(batch.size()) + " chunks");
            }
            std::ranges::move(embedded, std::back_inserter(vectors));
            if (progress) {
                progress(end, texts.size());
            }
        }
    } catch (const harness::CancelledError&) {
        return std::nullopt;
    } catch (const std::exception& e) {
        vectors.clear();
        note = std::string{name} + ": embedding failed (" + std::string{e.what()} +
               "), so it is searched by its words only";
    }
    return vectors;
}

AttachmentIndex::Added AttachmentIndex::add(const FoundFile& file,
                                            const harness::CancellationToken& cancellation,
                                            const Progress& progress) {
    Added out;
    const std::string bytes = read_bytes(file.path);
    if (bytes.empty()) {
        out.skip = file.name + ": empty or unreadable";
        return out;
    }
    const std::string sha256 = models::sha256_hex(bytes);
    {
        std::error_code code;
        std::filesystem::create_directories(store_path_.parent_path(), code);
    }
    if (holds(sha256)) {
        return held(file, sha256);
    }
    if (Added copied = copy_from_others(file, sha256); copied.file.has_value()) {
        return copied;
    }

    // An image, audio or a video is read by a model into its text form --
    // a description, a transcript, a timeline (26e); everything else as text.
    const std::optional<harness::Medium> medium = medium_of(file.path);
    AttachmentText read;
    try {
        read = medium.has_value() && media_ ? media_(file, *medium, cancellation)
                                            : read_attachment_text(file.path);
    } catch (const harness::CancelledError&) {
        out.skip = file.name + ": cancelled";
        return out;
    }
    for (const std::string& note : read.notes) {
        out.note += (out.note.empty() ? "" : "\n") + note;
    }
    if (cancellation.stop_requested()) {
        out.skip = file.name + ": cancelled";
        return out;
    }
    if (!read.ok()) {
        out.skip = file.name + ": " + read.reason;
        return out;
    }
    const ChunkedFile chunked = chunk_file(read, file.name, sha256);
    if (chunked.texts.empty()) {
        out.skip = file.name + ": no text";
        return out;
    }
    const std::optional<std::vector<std::vector<float>>> vectors =
        embed_chunks(chunked.texts, file.name, cancellation, progress, out.note);
    if (!vectors.has_value() || cancellation.stop_requested()) {
        out.skip = file.name + ": cancelled";
        return out;
    }

    embedstore::Store store{store_path_};
    if (!vectors->empty() && !store.embedding_model().recorded()) {
        store.set_embedding_model(model(), static_cast<std::int64_t>(vectors->front().size()));
    }
    store.replace_source(attachment_source(sha256), chunked.texts, *vectors, chunked.metadata);
    out.file = logger::AttachedFile{.name = file.name,
                                    .path = file.path.string(),
                                    .sha256 = sha256,
                                    .reader = read.reader,
                                    .bytes = file.bytes};
    out.chunks = static_cast<std::int64_t>(chunked.texts.size());
    out.vectorised = !vectors->empty();
    return out;
}

std::string AttachmentIndex::text_of(std::string_view sha256) const {
    std::error_code code;
    if (!std::filesystem::exists(store_path_, code)) {
        return {};
    }
    const embedstore::Store store{store_path_};
    const std::vector<embedstore::Chunk> chunks = store.chunks_by_source(attachment_source(sha256));
    if (chunks.empty()) {
        return {};
    }
    std::string text = chunks.front().text;
    std::int64_t end = metadata_of(chunks.front()).value("end", std::int64_t{0});
    for (std::size_t index = 1; index < chunks.size(); ++index) {
        const nlohmann::json meta = metadata_of(chunks[index]);
        append_after(text, end, chunks[index], meta);
        end = meta.value("end", end);
    }
    return text;
}

void AttachmentIndex::remove(std::string_view sha256) {
    std::error_code code;
    if (!std::filesystem::exists(store_path_, code)) {
        return;
    }
    embedstore::Store store{store_path_};
    (void)store.delete_source(attachment_source(sha256));
}

std::int64_t inline_tokens(const TurnBudget& budget, std::string_view name, std::string_view text) {
    harness::ChatRequest request;
    request.messages.push_back(harness::ChatMessage::user(render_inline_attachment(name, text)));
    return budget.tokens(request).tokens;
}

bool fits_inline(const TurnBudget& budget, std::int64_t already, std::int64_t tokens) {
    return budget.budget.known() &&
           already + tokens <= budget.budget.share(BudgetSource::Attachments);
}

}  // namespace apogee::agentloop
