#include "models/source_ollama.h"

#include <nlohmann/json.hpp>

#include <cstdlib>
#include <fstream>
#include <system_error>

#include "platform/platform.h"

namespace apogee::models {
namespace {

constexpr std::string_view kMediaTypeModel = "application/vnd.ollama.image.model";
constexpr std::string_view kMediaTypeTemplate = "application/vnd.ollama.image.template";
constexpr std::string_view kDefaultRegistry = "registry.ollama.ai";
constexpr std::string_view kLibraryNamespace = "library";
constexpr std::size_t kCopyChunk = 1U << 20U;  // 1 MiB

/// "sha256:abc" -> "sha256-abc", which is how blobs are named on disk.
[[nodiscard]] std::string blob_filename(std::string_view digest) {
    std::string name{digest};
    if (const std::size_t colon = name.find(':'); colon != std::string::npos) {
        name[colon] = '-';
    }
    return name;
}

[[nodiscard]] std::string strip_algorithm(std::string_view digest) {
    if (const std::size_t colon = digest.find(':'); colon != std::string_view::npos) {
        digest.remove_prefix(colon + 1);
    }
    return std::string{digest};
}

[[nodiscard]] std::string read_file(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return {};
    }
    return std::string{std::istreambuf_iterator<char>{in}, std::istreambuf_iterator<char>{}};
}

}  // namespace

std::filesystem::path ollama_store_root() {
    // Ollama's own override, honoured so a user who moved their store is not
    // told their models are missing.
    if (const char* override_path = std::getenv("OLLAMA_MODELS");
        override_path != nullptr && *override_path != '\0') {
        return std::filesystem::path{override_path};
    }
    const std::optional<std::string> home = platform::home_directory();
    if (!home.has_value()) {
        // No home means no store. Returning a relative path would silently
        // look in the working directory, which is worse than finding nothing.
        return {};
    }
    return std::filesystem::path{*home} / ".ollama" / "models";
}

std::pair<std::string, std::string> split_ref(std::string_view ref) {
    const std::size_t colon = ref.rfind(':');
    // A colon inside a namespace ("host:port/model") is not a tag separator;
    // only a colon after the last slash is.
    const std::size_t slash = ref.rfind('/');
    if (colon == std::string_view::npos || (slash != std::string_view::npos && colon < slash)) {
        return {std::string{ref}, "latest"};
    }
    return {std::string{ref.substr(0, colon)}, std::string{ref.substr(colon + 1)}};
}

std::filesystem::path manifest_path(const std::filesystem::path& root, std::string_view ref) {
    const auto [name, tag] = split_ref(ref);

    std::filesystem::path path = root / "manifests" / std::string{kDefaultRegistry};
    if (const std::size_t slash = name.find('/'); slash != std::string::npos) {
        path /= name.substr(0, slash);
        path /= name.substr(slash + 1);
    } else {
        path /= std::string{kLibraryNamespace};
        path /= name;
    }
    return path / tag;
}

std::optional<OllamaEntry> parse_manifest(const std::filesystem::path& root, std::string_view ref,
                                          std::string_view manifest_json) {
    const nlohmann::json root_json = nlohmann::json::parse(manifest_json, nullptr, false);
    if (root_json.is_discarded() || !root_json.is_object()) {
        return std::nullopt;
    }

    const auto layers = root_json.find("layers");
    if (layers == root_json.end() || !layers->is_array()) {
        return std::nullopt;
    }

    OllamaEntry entry;
    entry.ref = std::string{ref};
    bool found_model = false;

    for (const nlohmann::json& layer : *layers) {
        if (!layer.is_object()) {
            continue;
        }
        const std::string media = layer.value("mediaType", std::string{});
        const std::string digest = layer.value("digest", std::string{});
        if (digest.empty()) {
            continue;
        }

        if (media == kMediaTypeModel) {
            entry.digest = strip_algorithm(digest);
            entry.size = layer.value("size", std::int64_t{0});
            entry.blob = root / "blobs" / blob_filename(digest);
            found_model = true;
        } else if (media == kMediaTypeTemplate) {
            // Recorded as a hint. Applying it would override the profile
            // layer's resolution ladder from outside the ladder.
            entry.template_hint = read_file(root / "blobs" / blob_filename(digest));
        }
    }

    if (!found_model) {
        // A cloud model's manifest has an empty `layers` array: the weights
        // live on Ollama's servers, so there is nothing here to copy. Reported
        // as "not local" rather than as a broken manifest, because it is not
        // broken -- it is a different kind of model.
        return std::nullopt;
    }
    return entry;
}

bool store_has_manifest(const std::filesystem::path& root, std::string_view ref) {
    std::error_code code;
    return std::filesystem::exists(manifest_path(root, ref), code);
}

std::optional<OllamaEntry> find_in_store(const std::filesystem::path& root, std::string_view ref) {
    const std::filesystem::path path = manifest_path(root, ref);
    std::error_code code;
    if (!std::filesystem::exists(path, code)) {
        return std::nullopt;
    }
    const std::string text = read_file(path);
    if (text.empty()) {
        return std::nullopt;
    }
    std::optional<OllamaEntry> entry = parse_manifest(root, ref, text);
    if (entry.has_value() && !std::filesystem::exists(entry->blob, code)) {
        // The manifest names a blob that is not there -- a store the user has
        // pruned by hand. Reported as absent rather than returned as a path
        // that would fail on open.
        return std::nullopt;
    }
    return entry;
}

std::vector<OllamaEntry> list_store(const std::filesystem::path& root) {
    std::vector<OllamaEntry> entries;
    std::error_code code;

    const std::filesystem::path manifests = root / "manifests";
    if (!std::filesystem::exists(manifests, code)) {
        return entries;
    }

    for (const auto& file : std::filesystem::recursive_directory_iterator(
             manifests, std::filesystem::directory_options::skip_permission_denied, code)) {
        if (code) {
            break;
        }
        if (!file.is_regular_file(code)) {
            continue;
        }
        // A manifest's path IS its ref: <registry>/<namespace>/<name>/<tag>.
        const std::filesystem::path relative =
            std::filesystem::relative(file.path(), manifests, code);
        if (code || relative.empty()) {
            continue;
        }

        std::vector<std::string> parts;
        for (const auto& part : relative) {
            parts.push_back(part.string());
        }
        if (parts.size() < 3) {
            continue;
        }
        // Drop the registry; join the namespace and name, then ":" and the tag.
        std::string ref;
        for (std::size_t i = 1; i + 1 < parts.size(); ++i) {
            if (!ref.empty()) {
                ref += "/";
            }
            ref += parts[i];
        }
        if (parts[1] == kLibraryNamespace) {
            ref = ref.substr(std::string{kLibraryNamespace}.size() + 1);
        }
        ref += ":" + parts.back();

        if (std::optional<OllamaEntry> entry = parse_manifest(root, ref, read_file(file.path()))) {
            if (std::filesystem::exists(entry->blob, code)) {
                entries.push_back(std::move(*entry));
            }
        }
    }
    return entries;
}

ByteSource blob_source(const OllamaEntry& entry) {
    const std::filesystem::path blob = entry.blob;
    return [blob](const std::function<bool(std::string_view)>& write, std::string& error) {
        std::ifstream in(blob, std::ios::binary);
        if (!in) {
            error = "could not open " + blob.string();
            return false;
        }
        std::string chunk(kCopyChunk, '\0');
        while (in) {
            in.read(chunk.data(), static_cast<std::streamsize>(chunk.size()));
            const std::streamsize got = in.gcount();
            if (got > 0 && !write(std::string_view{chunk.data(), static_cast<std::size_t>(got)})) {
                error = "could not write the copied bytes";
                return false;
            }
        }
        if (in.bad()) {
            error = "error reading " + blob.string();
            return false;
        }
        return true;
    };
}

SourcePromise promise_for(const OllamaEntry& entry) {
    SourcePromise promise;
    promise.ref = entry.ref;
    promise.source = "ollama";
    promise.source_url = entry.blob.string();
    // Ollama's blobs are content-addressed, so unlike most sources this one
    // really does publish a digest -- and the ladder gets to use it.
    promise.digest = entry.digest;
    promise.size = entry.size;
    promise.template_hint = entry.template_hint;
    return promise;
}

}  // namespace apogee::models
