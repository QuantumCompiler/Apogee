#include "embedstore/ingest.h"

#include <algorithm>
#include <exception>
#include <fstream>
#include <system_error>

#include "embedstore/store.h"
#include "platform/child_process.h"

namespace apogee::embedstore {
namespace {

constexpr std::size_t kSniffBytes = 8192;

[[nodiscard]] std::string read_file(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return {};
    }
    return std::string{std::istreambuf_iterator<char>{in}, std::istreambuf_iterator<char>{}};
}

/// Runs `pdftotext` and returns its stdout, or nullopt.
[[nodiscard]] bool run_pdftotext(const std::filesystem::path& path, std::string& out) {
    if (!platform::supports_child_processes()) {
        return false;
    }
    platform::ChildCommand command;
    command.program = "pdftotext";
    // "-" sends the text to stdout instead of writing a sibling .txt file --
    // ingesting a directory must not litter it with converted copies.
    command.arguments = {"-q", path.string(), "-"};

    std::string error;
    std::unique_ptr<platform::ChildProcess> child = platform::start_child(command, error);
    if (child == nullptr) {
        return false;
    }
    child->close_stdin();

    std::string chunk;
    for (;;) {
        const platform::ReadStatus status =
            child->read_stdout(chunk, std::chrono::milliseconds{200});
        if (status == platform::ReadStatus::Data) {
            out += chunk;
            continue;
        }
        if (status == platform::ReadStatus::Timeout) {
            continue;
        }
        break;
    }
    const std::optional<int> status = child->wait_for_exit(std::chrono::seconds{5});
    return status.has_value() && *status == 0 && !out.empty();
}

}  // namespace

bool looks_binary(std::string_view bytes) noexcept {
    const std::size_t limit = std::min(bytes.size(), kSniffBytes);
    return bytes.substr(0, limit).find('\0') != std::string_view::npos;
}

bool is_pdf(const std::filesystem::path& path) {
    std::string extension = path.extension().string();
    std::ranges::transform(extension, extension.begin(),
                           [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return extension == ".pdf";
}

bool pdftotext_available() {
    return platform::supports_child_processes() && !platform::find_on_path("pdftotext").empty();
}

bool read_as_text(const std::filesystem::path& path, std::string& text, std::string& reason) {
    if (is_pdf(path)) {
        if (!pdftotext_available()) {
            // Skipped with a REASON, not silently. A PDF among Markdown must
            // not stop the Markdown, and the user must be able to see why the
            // PDF is missing from answers.
            reason = "needs pdftotext, which is not installed";
            return false;
        }
        text.clear();
        if (!run_pdftotext(path, text)) {
            reason = "pdftotext could not read it";
            return false;
        }
        return true;
    }

    text = read_file(path);
    if (text.empty()) {
        reason = "empty or unreadable";
        return false;
    }
    if (looks_binary(text)) {
        reason = "looks binary";
        return false;
    }
    return true;
}

IngestReport ingest_path(const std::filesystem::path& store_path,
                         const std::filesystem::path& target, const ChunkOptions& options,
                         const EmbedChunks& embed) {
    IngestReport report;
    Store store{store_path};

    std::error_code code;
    std::vector<std::filesystem::path> files;
    std::filesystem::path root = target;

    if (std::filesystem::is_directory(target, code)) {
        for (const auto& entry : std::filesystem::recursive_directory_iterator(
                 target, std::filesystem::directory_options::skip_permission_denied, code)) {
            if (code) {
                break;
            }
            if (entry.is_regular_file(code)) {
                files.push_back(entry.path());
            }
        }
        std::ranges::sort(files);
    } else {
        files.push_back(target);
        root = target.parent_path();
    }

    for (const std::filesystem::path& file : files) {
        std::string text;
        std::string reason;
        if (!read_as_text(file, text, reason)) {
            ++report.files_skipped;
            report.skips.push_back(file.filename().string() + ": " + reason);
            continue;
        }

        // Relative to the ingest root, so a corpus survives being moved and two
        // machines record the same source names for the same documents.
        const std::filesystem::path relative = std::filesystem::relative(file, root, code);
        const std::string source = code || relative.empty() ? file.string() : relative.string();

        const std::vector<std::string> chunks = chunk_text(text, options);
        if (embed) {
            // Vectors for this source, or -- if the embedder fails -- the
            // chunks stored lexical-only and the failure NAMED, so the store
            // reports partial coverage rather than hiding a hole.
            std::vector<std::vector<float>> vectors;
            std::string trouble;
            try {
                vectors = embed(chunks);
                if (vectors.size() != chunks.size()) {
                    trouble = "embedder returned " + std::to_string(vectors.size()) +
                              " vector(s) for " + std::to_string(chunks.size()) + " chunk(s)";
                    vectors.clear();
                }
            } catch (const std::exception& e) {
                trouble = e.what();
                vectors.clear();
            }
            if (!trouble.empty()) {
                report.unvectorised.push_back(source + ": " + trouble);
            } else {
                report.vectors_written += static_cast<std::int64_t>(vectors.size());
            }
            store.replace_source(source, chunks, vectors);
        } else {
            store.replace_source(source, chunks);
        }

        ++report.files_read;
        report.chunks_written += static_cast<std::int64_t>(chunks.size());
    }

    return report;
}

}  // namespace apogee::embedstore
