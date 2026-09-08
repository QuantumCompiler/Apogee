#include "models/acquire.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <system_error>
#include <utility>

#include "models/gguf_inspect.h"
#include "models/sha256.h"

namespace apogee::models {
namespace {

/// RFC3339, UTC.
[[nodiscard]] std::string now_rfc3339() {
    const auto now = std::chrono::system_clock::now();
    const std::time_t as_time = std::chrono::system_clock::to_time_t(now);
    std::tm utc{};
#if defined(_WIN32)
    gmtime_s(&utc, &as_time);
#else
    gmtime_r(&as_time, &utc);
#endif
    std::ostringstream out;
    out << std::put_time(&utc, "%Y-%m-%dT%H:%M:%SZ");
    return out.str();
}

[[nodiscard]] std::string lowercase(std::string value) {
    std::ranges::transform(value, value.begin(),
                           [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return value;
}

/// Strips a "sha256:" prefix and lowercases, so a digest from a manifest and
/// one from an HTTP header compare equal.
[[nodiscard]] std::string normalize_digest(std::string_view value) {
    constexpr std::string_view prefix = "sha256:";
    if (value.starts_with(prefix)) {
        value.remove_prefix(prefix.size());
    }
    return lowercase(std::string{value});
}

/// Removes a path, ignoring failure. Used on the cleanup path, where a second
/// error must not mask the first.
void remove_quietly(const std::filesystem::path& path) {
    std::error_code code;
    std::filesystem::remove(path, code);
}

}  // namespace

bool is_known_unrunnable(std::string_view architecture) noexcept {
    // Advisory, and deliberately short. Every entry is a family observed not to
    // load in a llama.cpp of roughly this vintage. It is a courtesy before a
    // large download, NOT a policy: `acquire` never consults it.
    static constexpr std::array<std::string_view, 3> known{
        "bert",  // embedding-only architectures the chat path cannot run
        "clip",  // a vision tower on its own
        "t5",    // encoder-decoder; llama.cpp's runner is decoder-only
    };
    // Compared without allocating: this function is noexcept, and the obvious
    // `lowercase(std::string{...})` would make a bad_alloc terminate the process
    // instead of throwing.
    return std::ranges::any_of(known, [architecture](std::string_view candidate) {
        return std::ranges::equal(architecture, candidate, [](char a, char b) {
            return std::tolower(static_cast<unsigned char>(a)) ==
                   std::tolower(static_cast<unsigned char>(b));
        });
    });
}

AcquireResult acquire(const std::filesystem::path& destination, const SourcePromise& promise,
                      const ByteSource& source, const ProgressFn& progress) {
    AcquireResult result;

    std::error_code code;
    if (std::filesystem::exists(destination, code)) {
        // Never a silent overwrite: the file may be the model a session is
        // using right now.
        result.error = "a file already exists at " + destination.string() +
                       " -- delete it first, or pull to a different name";
        return result;
    }

    std::filesystem::create_directories(destination.parent_path(), code);
    if (code) {
        result.error =
            "could not create " + destination.parent_path().string() + ": " + code.message();
        return result;
    }

    std::filesystem::path partial = destination;
    partial += ".partial";
    // A leftover partial from an earlier interrupted run is not resumed here:
    // resumption belongs to the source (it needs ranged requests), and a
    // partial of unknown provenance is exactly the input this whole ladder
    // exists to reject.
    remove_quietly(partial);

    // --- rung 1: stream to the partial --------------------------------------
    Sha256 hash;
    std::int64_t written = 0;
    {
        std::ofstream out(partial, std::ios::binary | std::ios::trunc);
        if (!out) {
            result.error = "could not open " + partial.string() + " for writing";
            return result;
        }

        const auto write = [&](std::string_view bytes) {
            out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
            if (!out.good()) {
                return false;
            }
            hash.update(bytes);
            written += static_cast<std::int64_t>(bytes.size());
            if (progress) {
                progress(written, promise.size);
            }
            return true;
        };

        std::string error;
        const bool produced = source(write, error);
        out.flush();
        if (!produced || !out.good()) {
            remove_quietly(partial);
            result.error = error.empty() ? "the transfer failed" : error;
            return result;
        }
    }

    Sidecar sidecar;
    sidecar.ref = promise.ref;
    sidecar.source = promise.source;
    sidecar.source_url = promise.source_url;
    sidecar.published_digest = normalize_digest(promise.digest);
    sidecar.published_size = promise.size;
    sidecar.pulled_at = now_rfc3339();
    sidecar.file = destination.filename().string();
    sidecar.file_size = written;
    sidecar.file_digest = hash.hex_digest();
    sidecar.template_hint = promise.template_hint;

    // --- rung 2: size, when one was declared --------------------------------
    if (promise.size > 0) {
        sidecar.verification.size_checked = true;
        sidecar.verification.size_matched = written == promise.size;
        if (!sidecar.verification.size_matched) {
            remove_quietly(partial);
            result.error = "size mismatch: expected " + std::to_string(promise.size) +
                           " bytes, got " + std::to_string(written);
            return result;
        }
    }

    // --- rung 3: digest, when one was published -----------------------------
    if (!sidecar.published_digest.empty()) {
        sidecar.verification.digest_checked = true;
        sidecar.verification.digest_matched = sidecar.file_digest == sidecar.published_digest;
        if (!sidecar.verification.digest_matched) {
            remove_quietly(partial);
            result.error = "digest mismatch: expected " + sidecar.published_digest + ", got " +
                           sidecar.file_digest;
            return result;
        }
    }

    // --- rung 4: the header actually parses ---------------------------------
    const GgufInfo info = inspect_gguf(partial);
    sidecar.verification.header_checked = true;
    sidecar.verification.header_parsed = info.parsed;
    if (!info.parsed) {
        remove_quietly(partial);
        result.error = "the downloaded file is not a readable GGUF -- " + info.parse_error;
        return result;
    }

    // --- rung 5: commit ------------------------------------------------------
    std::filesystem::rename(partial, destination, code);
    if (code) {
        remove_quietly(partial);
        result.error = "could not move the finished file into place: " + code.message();
        return result;
    }

    // The sidecar goes last, so it can never describe a file that is not there.
    // A failure here leaves a usable model with unknown provenance, which is a
    // warning rather than a reason to delete what was just downloaded.
    if (!write_sidecar(destination, sidecar)) {
        result.ok = true;
        result.path = destination;
        result.sidecar = sidecar;
        result.error = "the model landed but its provenance record could not be written";
        return result;
    }

    result.ok = true;
    result.path = destination;
    result.sidecar = sidecar;
    return result;
}

Verification reverify(const std::filesystem::path& model, const Sidecar& sidecar) {
    Verification verification;

    std::error_code code;
    const std::uintmax_t size = std::filesystem::file_size(model, code);
    if (code) {
        // Nothing could be checked. Every flag stays false, which `sound()`
        // reads as "no failures" -- so the caller must check existence first.
        return verification;
    }

    // Compared against what the file should be ON DISK, not against what the
    // source published: a transformed file legitimately differs from its pin,
    // and this is the distinction the sidecar's two-field split exists for.
    if (const std::int64_t expected = sidecar.expected_size(); expected > 0) {
        verification.size_checked = true;
        verification.size_matched = std::cmp_equal(size, expected);
    }

    if (const std::string expected = sidecar.expected_digest(); !expected.empty()) {
        verification.digest_checked = true;
        verification.digest_matched = file_sha256(model) == normalize_digest(expected);
    }

    const GgufInfo info = inspect_gguf(model);
    verification.header_checked = true;
    verification.header_parsed = info.parsed;

    return verification;
}

}  // namespace apogee::models
