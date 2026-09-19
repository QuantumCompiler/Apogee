#include "models/source_hf.h"

#include <nlohmann/json.hpp>

#include <cstdlib>
#include <functional>

namespace apogee::models {
namespace {

constexpr std::string_view kApiBase = "https://huggingface.co";

[[nodiscard]] std::vector<backends::HttpHeader> headers_for(std::string_view token) {
    std::vector<backends::HttpHeader> headers;
    if (!token.empty()) {
        // Read at the point of use, sent, and never written anywhere. There is
        // no token store in Apogee and this item does not add one.
        headers.push_back({.name = "Authorization", .value = "Bearer " + std::string{token}});
    }
    return headers;
}

}  // namespace

std::string hf_token(std::string_view configured) {
    if (!configured.empty()) {
        return std::string{configured};
    }
    for (const char* name : {"HF_TOKEN", "HUGGING_FACE_HUB_TOKEN"}) {
        if (const char* value = std::getenv(name); value != nullptr && *value != '\0') {
            return std::string{value};
        }
    }
    return {};
}

std::optional<HfRef> parse_hf_ref(std::string_view ref) {
    HfRef parsed;

    // The file, if named: everything after the last ':' that follows the '/'.
    if (const std::size_t colon = ref.rfind(':'); colon != std::string_view::npos) {
        const std::size_t slash = ref.find('/');
        if (slash != std::string_view::npos && colon > slash) {
            parsed.file = std::string{ref.substr(colon + 1)};
            ref = ref.substr(0, colon);
        }
    }

    // The revision, if named.
    if (const std::size_t at = ref.rfind('@'); at != std::string_view::npos) {
        parsed.revision = std::string{ref.substr(at + 1)};
        ref = ref.substr(0, at);
    }

    const std::size_t slash = ref.find('/');
    if (slash == std::string_view::npos || slash == 0 || slash + 1 >= ref.size()) {
        return std::nullopt;
    }
    parsed.owner = std::string{ref.substr(0, slash)};
    parsed.repo = std::string{ref.substr(slash + 1)};
    if (parsed.repo.find('/') != std::string::npos) {
        return std::nullopt;
    }
    return parsed;
}

std::string hf_download_url(const HfRef& ref) {
    return hf_download_url(ref, HfRepoKind::Model);
}

std::string hf_download_url(const HfRef& ref, HfRepoKind kind) {
    const std::string revision = ref.revision.empty() ? "main" : ref.revision;
    const std::string prefix = kind == HfRepoKind::Dataset ? "/datasets/" : "/";
    return std::string{kApiBase} + prefix + ref.repo_id() + "/resolve/" + revision + "/" + ref.file;
}

HfTree list_repo_tree(backends::HttpClient& client, const HfRef& ref, HfRepoKind kind,
                      std::string_view token, const harness::CancellationToken& cancellation) {
    HfTree tree;
    const std::string revision = ref.revision.empty() ? "main" : ref.revision;
    backends::HttpRequest request;
    request.method = "GET";
    request.url = std::string{kApiBase} +
                  (kind == HfRepoKind::Dataset ? "/api/datasets/" : "/api/models/") +
                  ref.repo_id() + "/tree/" + revision + "?recursive=true";
    request.headers = headers_for(token);
    request.timeout = std::chrono::seconds{60};

    backends::HttpResponse response;
    try {
        response = client.send(request, {}, cancellation);
    } catch (const backends::HttpError& e) {
        tree.error = std::string{"could not reach Hugging Face: "} + e.what();
        return tree;
    }
    if (response.status == 401 || response.status == 403) {
        tree.error = "cannot read '" + ref.repo_id() +
                     "'. Check the spelling; if it is correct, the repository is gated or "
                     "private -- accept its licence on huggingface.co and set HF_TOKEN";
        return tree;
    }
    if (response.status == 404) {
        tree.error =
            "no such repository or revision: '" + ref.repo_id() + "' at '" + revision + "'";
        return tree;
    }
    if (!response.ok()) {
        tree.error = "Hugging Face returned status " + std::to_string(response.status);
        return tree;
    }
    const nlohmann::json root = nlohmann::json::parse(response.body, nullptr, false);
    if (root.is_discarded() || !root.is_array()) {
        tree.error = "Hugging Face returned something that is not a file listing";
        return tree;
    }
    for (const nlohmann::json& item : root) {
        if (!item.is_object() || item.value("type", std::string{}) != "file") {
            continue;
        }
        HfFile file;
        file.path = item.value("path", std::string{});
        if (file.path.empty()) {
            continue;
        }
        if (const auto size = item.find("size"); size != item.end() && size->is_number_integer()) {
            file.size = size->get<std::int64_t>();
        }
        if (const auto lfs = item.find("lfs"); lfs != item.end() && lfs->is_object()) {
            // The LFS oid IS the sha256 of the bytes; the plain oid is a git
            // blob hash, which is not, so it is deliberately not read.
            file.sha256 = lfs->value("oid", std::string{});
            if (const auto size = lfs->find("size");
                size != lfs->end() && size->is_number_integer()) {
                file.size = size->get<std::int64_t>();
            }
        }
        tree.files.push_back(std::move(file));
    }
    tree.ok = true;
    return tree;
}

bool snapshot_wanted(std::string_view path) noexcept {
    const std::size_t slash = path.rfind('/');
    const std::string_view name = slash == std::string_view::npos ? path : path.substr(slash + 1);
    if (name.empty() || name.front() == '.') {
        return false;
    }
    for (const std::string_view suffix :
         {".safetensors", ".json", ".model", ".txt", ".py", ".tiktoken", ".jinja"}) {
        if (name.ends_with(suffix)) {
            return name != "README.md";
        }
    }
    return false;
}

bool dataset_file_wanted(std::string_view path) noexcept {
    const std::size_t slash = path.rfind('/');
    const std::string_view name = slash == std::string_view::npos ? path : path.substr(slash + 1);
    if (name.empty() || name.front() == '.') {
        return false;
    }
    for (const std::string_view suffix :
         {".parquet", ".jsonl", ".json", ".csv", ".arrow", ".txt"}) {
        if (name.ends_with(suffix)) {
            return name != "README.md";
        }
    }
    return false;
}

std::string repo_directory_name(const HfRef& ref) {
    std::string name = ref.owner + "--" + ref.repo;
    for (char& c : name) {
        if (c == '/' || c == ':' || c == '@' || c == ' ' || c == '\\') {
            c = '-';
        }
    }
    return name;
}

HfListing list_gguf_files(backends::HttpClient& client, const HfRef& ref, std::string_view token,
                          const harness::CancellationToken& cancellation) {
    HfListing listing;

    backends::HttpRequest request;
    request.method = "GET";
    request.url = std::string{kApiBase} + "/api/models/" + ref.repo_id();
    if (!ref.revision.empty()) {
        request.url += "/revision/" + ref.revision;
    }
    request.headers = headers_for(token);
    request.timeout = std::chrono::seconds{60};

    backends::HttpResponse response;
    try {
        response = client.send(request, {}, cancellation);
    } catch (const backends::HttpError& e) {
        listing.error = std::string{"could not reach Hugging Face: "} + e.what();
        return listing;
    }

    if (response.status == 401 || response.status == 403) {
        // Hugging Face answers 401 for a repository that does not exist as well
        // as for one that is gated -- it will not leak which. Asserting "gated"
        // here sends someone with a typo hunting for a licence to accept, so
        // the message names both possibilities in the order they occur.
        listing.error = "cannot read '" + ref.repo_id() +
                        "'. Check the spelling; if it is correct, the repository is gated or "
                        "private -- accept its licence on huggingface.co and set HF_TOKEN";
        return listing;
    }
    if (response.status == 404) {
        listing.error = "no such repository: '" + ref.repo_id() + "'";
        return listing;
    }
    if (!response.ok()) {
        listing.error = "Hugging Face returned status " + std::to_string(response.status);
        return listing;
    }

    const nlohmann::json root = nlohmann::json::parse(response.body, nullptr, false);
    if (root.is_discarded() || !root.is_object()) {
        listing.error = "Hugging Face returned something that is not a repository description";
        return listing;
    }

    if (const auto siblings = root.find("siblings");
        siblings != root.end() && siblings->is_array()) {
        for (const nlohmann::json& sibling : *siblings) {
            if (!sibling.is_object()) {
                continue;
            }
            const std::string name = sibling.value("rfilename", std::string{});
            if (name.size() > 5 && name.ends_with(".gguf")) {
                listing.gguf_files.push_back(name);
            } else if (name.ends_with(".safetensors")) {
                // Noticed here so the GGUF refusal can name the next step;
                // downloaded whole by `models pull --safetensors`.
                listing.has_safetensors = true;
                listing.safetensors_files.push_back(name);
            }
        }
    }
    listing.ok = true;
    return listing;
}

bool resolve_file(backends::HttpClient& client, HfRef& ref, std::string_view token,
                  const harness::CancellationToken& cancellation, std::string& error) {
    if (!ref.file.empty()) {
        return true;
    }

    const HfListing listing = list_gguf_files(client, ref, token, cancellation);
    if (!listing.ok) {
        error = listing.error;
        return false;
    }
    if (listing.gguf_files.empty()) {
        // Naming the conversion path matters: SPEC lists SafeTensors in scope,
        // so "no .gguf" alone reads as a limitation rather than as a step the
        // user can take. Apogee does not run the converter itself -- it needs a
        // Python environment with torch and transformers, which a C++ harness
        // cannot assume is present and should not install on someone's behalf.
        error = "'" + ref.repo_id() + "' contains no .gguf file.\n\n";
        if (listing.has_safetensors) {
            error +=
                "It is a SafeTensors repository. Apogee runs GGUF, so it needs converting "
                "first:\n"
                "  python convert_hf_to_gguf.py --outfile model.gguf <the downloaded repo>\n"
                "(that script ships with llama.cpp and needs Python with torch and "
                "transformers)\n\n"
                "To download the full-weight snapshot itself -- for fine-tuning -- pass "
                "--safetensors:\n"
                "  apogee models pull " +
                ref.repo_id() +
                " --safetensors\n\n"
                "Or look for a community GGUF conversion -- searching the model's name with "
                "\"GGUF\" usually finds one.";
        } else {
            error += "Apogee runs GGUF models. Look for a GGUF conversion of this model.";
        }
        return false;
    }
    if (listing.gguf_files.size() > 1) {
        // Deliberately a refusal, not a guess. A quantised upload holds a dozen
        // precisions differing by gigabytes and by quality; choosing one for
        // the user spends their bandwidth on a file they did not pick.
        error = "'" + ref.repo_id() + "' contains " + std::to_string(listing.gguf_files.size()) +
                " GGUF files. Name one, e.g. " + ref.repo_id() + ":" + listing.gguf_files.front() +
                "\n\nAvailable:";
        for (const std::string& file : listing.gguf_files) {
            error += "\n  " + file;
        }
        return false;
    }

    ref.file = listing.gguf_files.front();
    return true;
}

ByteSource http_source(backends::HttpClient& client, const HfRef& ref, HfRepoKind kind,
                       const HfFile& file, std::string_view token,
                       const harness::CancellationToken& cancellation, SourcePromise& promise) {
    HfRef named = ref;
    named.file = file.path;
    ByteSource source = http_source(client, named, token, cancellation, promise);
    promise.source_url = hf_download_url(named, kind);
    promise.size = file.size;
    promise.digest = file.sha256;
    const std::string url = promise.source_url;
    const std::string token_copy{token};
    if (kind == HfRepoKind::Model) {
        return source;
    }
    // A dataset file downloads from its own prefix; the rest of the source
    // is the same request.
    return [&client, url, token_copy, &cancellation](
               const std::function<bool(std::string_view)>& write, std::string& error) {
        backends::HttpRequest request;
        request.method = "GET";
        request.url = url;
        request.headers = headers_for(token_copy);
        request.timeout = std::chrono::seconds{0};
        backends::HttpResponse response;
        try {
            response = client.send(
                request, [&write](std::string_view chunk) { return write(chunk); }, cancellation);
        } catch (const backends::HttpError& e) {
            error = std::string{"the download failed: "} + e.what();
            return false;
        }
        if (response.status == 401 || response.status == 403) {
            error =
                "access denied. The repository may be gated -- accept its licence on "
                "huggingface.co and set HF_TOKEN";
            return false;
        }
        if (response.status == 404) {
            error = "no such file at " + url;
            return false;
        }
        if (!response.ok()) {
            error = "Hugging Face returned status " + std::to_string(response.status);
            return false;
        }
        return true;
    };
}

ByteSource http_source(backends::HttpClient& client, const HfRef& ref, std::string_view token,
                       const harness::CancellationToken& cancellation, SourcePromise& promise) {
    const std::string url = hf_download_url(ref);
    const std::string token_copy{token};

    promise.ref = ref.repo_id() + ":" + ref.file;
    promise.source = "huggingface";
    promise.source_url = url;
    // Hugging Face publishes a size for most files but an ETag that is only
    // sometimes a sha256, so no digest is promised here. The ladder reports
    // "no digest published", which for this source is the truth.

    return [&client, url, token_copy, &cancellation](
               const std::function<bool(std::string_view)>& write, std::string& error) {
        backends::HttpRequest request;
        request.method = "GET";
        request.url = url;
        request.headers = headers_for(token_copy);
        // No whole-request timeout: a multi-gigabyte transfer is not a hung
        // connection, and a timeout that cannot tell them apart aborts real
        // downloads. The connect timeout still bounds an unreachable host.
        request.timeout = std::chrono::seconds{0};

        backends::HttpResponse response;
        try {
            response = client.send(
                request, [&write](std::string_view chunk) { return write(chunk); }, cancellation);
        } catch (const backends::HttpError& e) {
            error = std::string{"the download failed: "} + e.what();
            return false;
        }

        if (response.status == 401 || response.status == 403) {
            error =
                "access denied. The repository may be gated -- accept its licence on "
                "huggingface.co and set HF_TOKEN";
            return false;
        }
        if (response.status == 404) {
            error = "no such file at " + url;
            return false;
        }
        if (!response.ok()) {
            error = "Hugging Face returned status " + std::to_string(response.status);
            return false;
        }
        return true;
    };
}

}  // namespace apogee::models
