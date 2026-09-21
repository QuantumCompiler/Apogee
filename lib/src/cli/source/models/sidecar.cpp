#include "models/sidecar.h"

#include <nlohmann/json.hpp>

#include <fstream>
#include <system_error>

#include "models/sha256.h"

namespace apogee::models {
namespace {

constexpr std::size_t kHashChunk = 1U << 20U;  // 1 MiB

}  // namespace

std::string Verification::summary() const {
    // Deliberately never the bare word "verified". What was checked IS the
    // information, and flattening three states into one word is what would
    // train a user to ignore it.
    std::string out;
    const auto add = [&out](std::string_view part) {
        if (!out.empty()) {
            out += ", ";
        }
        out += part;
    };

    if (size_checked) {
        add(size_matched ? "size ok" : "SIZE MISMATCH");
    }
    if (digest_checked) {
        add(digest_matched ? "digest ok" : "DIGEST MISMATCH");
    } else {
        add("no digest published");
    }
    if (header_checked) {
        add(header_parsed ? "header ok" : "HEADER UNREADABLE");
    }
    return out.empty() ? "nothing checked" : out;
}

std::string Sidecar::expected_digest() const {
    return file_digest.empty() ? published_digest : file_digest;
}

std::int64_t Sidecar::expected_size() const {
    return file_size > 0 ? file_size : published_size;
}

std::filesystem::path sidecar_path_for(const std::filesystem::path& model) {
    std::filesystem::path path = model;
    path.replace_extension(".json");
    return path;
}

std::string serialize(const Sidecar& sidecar) {
    nlohmann::json out;
    out["ref"] = sidecar.ref;
    out["source"] = sidecar.source;
    out["source_url"] = sidecar.source_url;
    out["pulled_at"] = sidecar.pulled_at;
    out["file"] = sidecar.file;
    out["file_digest"] = sidecar.file_digest;
    out["file_size"] = sidecar.file_size;

    // Written even when empty, so a reader can tell "no digest was published"
    // from "this sidecar predates digests" without guessing.
    out["published_digest"] = sidecar.published_digest;
    out["published_size"] = sidecar.published_size;

    if (!sidecar.transform.empty()) {
        out["transform"] = sidecar.transform;
        out["transform_note"] = sidecar.transform_note;
    }
    if (!sidecar.template_hint.empty()) {
        // Recorded, never auto-applied -- see the header.
        out["template_hint"] = sidecar.template_hint;
    }

    nlohmann::json verification;
    verification["size_checked"] = sidecar.verification.size_checked;
    verification["size_matched"] = sidecar.verification.size_matched;
    verification["digest_checked"] = sidecar.verification.digest_checked;
    verification["digest_matched"] = sidecar.verification.digest_matched;
    verification["header_checked"] = sidecar.verification.header_checked;
    verification["header_parsed"] = sidecar.verification.header_parsed;
    out["verification"] = std::move(verification);

    return out.dump(2) + "\n";
}

std::optional<Sidecar> parse_sidecar(std::string_view text) {
    const nlohmann::json root = nlohmann::json::parse(text, nullptr, false);
    if (root.is_discarded() || !root.is_object()) {
        return std::nullopt;
    }

    Sidecar sidecar;
    sidecar.ref = root.value("ref", std::string{});
    sidecar.source = root.value("source", std::string{});
    sidecar.source_url = root.value("source_url", std::string{});
    sidecar.published_digest = root.value("published_digest", std::string{});
    sidecar.published_size = root.value("published_size", std::int64_t{0});
    sidecar.pulled_at = root.value("pulled_at", std::string{});
    sidecar.file = root.value("file", std::string{});
    sidecar.file_digest = root.value("file_digest", std::string{});
    sidecar.file_size = root.value("file_size", std::int64_t{0});
    sidecar.transform = root.value("transform", std::string{});
    sidecar.transform_note = root.value("transform_note", std::string{});
    sidecar.template_hint = root.value("template_hint", std::string{});

    if (const auto verification = root.find("verification");
        verification != root.end() && verification->is_object()) {
        sidecar.verification.size_checked = verification->value("size_checked", false);
        sidecar.verification.size_matched = verification->value("size_matched", false);
        sidecar.verification.digest_checked = verification->value("digest_checked", false);
        sidecar.verification.digest_matched = verification->value("digest_matched", false);
        sidecar.verification.header_checked = verification->value("header_checked", false);
        sidecar.verification.header_parsed = verification->value("header_parsed", false);
    }
    return sidecar;
}

std::optional<Sidecar> load_sidecar(const std::filesystem::path& model) {
    const std::filesystem::path path = sidecar_path_for(model);
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return std::nullopt;
    }
    const std::string text{std::istreambuf_iterator<char>{in}, std::istreambuf_iterator<char>{}};
    return parse_sidecar(text);
}

bool write_sidecar(const std::filesystem::path& model, const Sidecar& sidecar) {
    const std::filesystem::path path = sidecar_path_for(model);
    // Temp-then-rename, the same discipline the config engine uses: a sidecar
    // half-written by an interrupted process would describe a model wrongly,
    // and a wrong provenance record is worse than none.
    std::filesystem::path temporary = path;
    temporary += ".tmp";

    {
        std::ofstream out(temporary, std::ios::binary | std::ios::trunc);
        if (!out) {
            return false;
        }
        const std::string text = serialize(sidecar);
        out.write(text.data(), static_cast<std::streamsize>(text.size()));
        if (!out.good()) {
            return false;
        }
    }

    std::error_code code;
    std::filesystem::rename(temporary, path, code);
    if (code) {
        std::filesystem::remove(temporary, code);
        return false;
    }
    return true;
}

std::string file_sha256(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return {};
    }
    Sha256 hash;
    std::string chunk(kHashChunk, '\0');
    while (in) {
        in.read(chunk.data(), static_cast<std::streamsize>(chunk.size()));
        const std::streamsize got = in.gcount();
        if (got > 0) {
            hash.update(std::string_view{chunk.data(), static_cast<std::size_t>(got)});
        }
    }
    if (in.bad()) {
        return {};
    }
    return hash.hex_digest();
}

}  // namespace apogee::models
