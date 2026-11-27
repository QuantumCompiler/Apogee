#include "embedstore/ingest.h"

#include <algorithm>
#include <exception>
#include <fstream>
#include <map>
#include <optional>
#include <set>
#include <system_error>
#include <utility>

#include "embedstore/folder_listing.h"
#include "embedstore/store.h"
#include "platform/child_process.h"
#include "platform/platform.h"

namespace apogee::embedstore {
namespace {

constexpr std::size_t kSniffBytes = 8192;

/// `path` in the generic form, as UTF-8 on every platform.
[[nodiscard]] std::string utf8_of(const std::filesystem::path& path) {
    const std::u8string text = path.generic_u8string();
    return std::string{text.begin(), text.end()};
}

/// Whether one of `left_out`'s entries -- a file, or a folder ending in '/'
/// -- names `source`.
[[nodiscard]] bool left_out_by(std::string_view source, const std::vector<std::string>& left_out) {
    return std::ranges::any_of(left_out, [source](const std::string& entry) {
        return entry == source || (entry.ends_with('/') && source.starts_with(entry));
    });
}

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

std::string folder_identity(const std::filesystem::path& root) {
    std::error_code code;
    std::filesystem::path absolute =
        root.empty() ? std::filesystem::current_path(code) : std::filesystem::absolute(root, code);
    if (code) {
        absolute = root;
    }
    std::filesystem::path resolved = std::filesystem::weakly_canonical(absolute, code);
    if (code) {
        resolved = absolute.lexically_normal();
    }
    // "/a/" and "/a" are one folder.
    if (!resolved.has_filename() && resolved.has_relative_path()) {
        resolved = resolved.parent_path();
    }
    return utf8_of(resolved);
}

IngestReport ingest_path(const std::filesystem::path& store_path,
                         const std::filesystem::path& target, const ChunkOptions& options,
                         const EmbedChunks& embed, const IngestProgress& progress,
                         std::string_view embed_model) {
    IngestReport report;
    Store store{store_path};

    // Each file, and the source it is recorded as: relative to the ingest
    // root, so a corpus survives being moved and two machines record the same
    // source names for the same documents -- in the generic form, with '/',
    // or a Windows machine would record "nested\\deep\\file.md" for what a
    // POSIX one calls "nested/deep/file.md".
    std::vector<std::pair<std::filesystem::path, std::string>> files;
    std::error_code code;
    const bool directory = std::filesystem::is_directory(target, code);
    if (directory) {
        // What a walk gives (57s): the attachment walk's listing, with
        // vendored folders left out too and a folder git ignores whole walked
        // because it was named. `.git/`, `.env` and `node_modules/` never
        // reach a chunk -- and from there a metered model -- by being near
        // what the person meant to share.
        FolderListing listing = list_folder(
            target,
            {.leave_out_vendored = true, .name_left_out = true, .walk_ignored_folder = true});
        files.reserve(listing.files.size());
        for (const std::filesystem::path& relative : listing.files) {
            files.emplace_back(target / relative, utf8_of(relative));
        }
        report.left_out = std::move(listing.left_out);
        report.root = folder_identity(target);
    } else {
        // Named on its own, so taken whatever its name: the rules are for
        // what a walk finds, never for what the person typed.
        files.emplace_back(target, utf8_of(target.filename()));
        report.root = folder_identity(target.parent_path());
    }

    // What the collection holds already, read before anything is written:
    // a same-named source from another folder is refused, not replaced.
    const std::vector<std::string> held_sources = store.sources();
    const std::set<std::string, std::less<>> held{held_sources.begin(), held_sources.end()};
    const std::map<std::string, Store::SourceIngest> recorded = store.source_ingests();

    std::size_t done = 0;
    for (const auto& [file, source] : files) {
        if (progress) {
            progress(++done, files.size(), file);
        }
        std::string text;
        std::string reason;
        if (!read_as_text(file, text, reason)) {
            ++report.files_skipped;
            report.skips.push_back(file.filename().string() + ": " + reason);
            continue;
        }

        // Checked before a chunk is embedded, so a refused file costs no call.
        std::optional<Replacement> replacing;
        if (held.contains(source)) {
            Replacement replacement{.source = source};
            if (const auto found = recorded.find(source); found == recorded.end()) {
                replacement.unrecorded = true;
            } else if (const std::string& root = found->second.root; root != report.root) {
                if (std::filesystem::exists(platform::path_from_utf8(root), code)) {
                    report.collisions.push_back({.source = source, .root = root});
                    continue;
                }
                // Its folder is gone: the corpus moved, and this is it.
                replacement.moved_from = root;
            }
            replacing = std::move(replacement);
        }

        const std::vector<std::string> chunks = chunk_text(text, options);
        std::vector<std::vector<float>> vectors;
        if (embed) {
            // Vectors for this source, or -- if the embedder fails -- the
            // chunks stored lexical-only and the failure NAMED, so the store
            // reports partial coverage rather than hiding a hole.
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
        }
        store.ingest_source(source, report.root, embed_model, chunks, vectors);
        report.written.push_back(source);
        if (replacing.has_value()) {
            report.replaced.push_back(std::move(*replacing));
        }

        ++report.files_read;
        report.chunks_written += static_cast<std::int64_t>(chunks.size());
    }

    if (directory) {
        // An earlier build took what this walk leaves out. Not removed on
        // Apogee's initiative -- named, with the way to remove each.
        const std::set<std::string, std::less<>> written{report.written.begin(),
                                                         report.written.end()};
        for (const std::string& source : store.sources()) {
            if (written.contains(source)) {
                continue;
            }
            const auto found = recorded.find(source);
            const bool this_folder = found == recorded.end() || found->second.root == report.root;
            if (hidden_path(platform::path_from_utf8(source)) ||
                !vendored_directory(source).empty() ||
                (this_folder && left_out_by(source, report.left_out))) {
                report.still_held.push_back(source);
            }
        }
    }

    return report;
}

}  // namespace apogee::embedstore
