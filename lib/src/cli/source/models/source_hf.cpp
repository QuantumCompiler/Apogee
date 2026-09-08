#include "models/source_hf.h"

#include <nlohmann/json.hpp>

#include <cstdlib>

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
    const std::string revision = ref.revision.empty() ? "main" : ref.revision;
    return std::string{kApiBase} + "/" + ref.repo_id() + "/resolve/" + revision + "/" + ref.file;
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
        error = "'" + ref.repo_id() +
                "' contains no .gguf file. Apogee runs GGUF models; a SafeTensors repository needs "
                "converting first";
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
