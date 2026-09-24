#include "models/convert.h"

#include <system_error>

#include "models/snapshot.h"

namespace apogee::models {
namespace {

void remove_quietly(const std::filesystem::path& path) {
    std::error_code code;
    std::filesystem::remove(path, code);
}

}  // namespace

std::filesystem::path conversion_partial_path(const std::filesystem::path& output) {
    std::filesystem::path partial = output;
    partial += ".partial";
    return partial;
}

std::int64_t estimated_gguf_bytes(std::int64_t elements, std::string_view out_type) {
    if (out_type == "f32") {
        return elements * 4;
    }
    if (out_type == "q8_0") {
        return elements / 32 * 34;
    }
    return elements * 2;  // f16, bf16, and what `auto` picks for a 16-bit source
}

std::string conversion_refusal(const std::filesystem::path& snapshot,
                               const std::filesystem::path& output) {
    // --- rung 1: a snapshot, and an intact one --------------------------------
    if (!is_snapshot_dir(snapshot)) {
        return snapshot.string() +
               " is not a SafeTensors snapshot (a directory holding config.json and at least one "
               "*.safetensors file). Pull one with 'apogee models pull <owner>/<repo> "
               "--safetensors'";
    }
    if (std::string damaged = damaged_snapshot_error(snapshot); !damaged.empty()) {
        return damaged;
    }
    // --- rung 2: never replace ----------------------------------------------
    std::error_code code;
    if (std::filesystem::exists(output, code)) {
        return "a file already exists at " + output.string() + " -- delete it first";
    }
    return {};
}

ConvertResult convert_snapshot(const std::filesystem::path& snapshot,
                               const std::filesystem::path& output, const ConvertFn& convert,
                               const harness::CancellationToken& cancellation) {
    ConvertResult result;
    result.error = conversion_refusal(snapshot, output);
    if (!result.error.empty()) {
        return result;
    }
    std::error_code code;
    if (!output.parent_path().empty()) {
        std::filesystem::create_directories(output.parent_path(), code);
        if (code) {
            result.error =
                "could not create " + output.parent_path().string() + ": " + code.message();
            return result;
        }
    }

    // --- rung 3: convert beside the destination -------------------------------
    const std::filesystem::path partial = conversion_partial_path(output);
    remove_quietly(partial);  // a leftover from an interrupted run is of unknown provenance
    const std::string failure = convert(snapshot, partial, cancellation);
    if (!failure.empty() || cancellation.stop_requested()) {
        remove_quietly(partial);
        result.cancelled = cancellation.stop_requested() || failure == "cancelled";
        result.error = result.cancelled ? "cancelled" : failure;
        return result;
    }

    // --- rung 4: the result is a GGUF -----------------------------------------
    result.info = inspect_gguf(partial);
    if (!result.info.parsed) {
        remove_quietly(partial);
        result.error = "the converter finished but its output is not a readable GGUF -- " +
                       result.info.parse_error;
        return result;
    }

    // --- rung 5: commit -------------------------------------------------------
    std::filesystem::rename(partial, output, code);
    if (code) {
        remove_quietly(partial);
        result.error = "could not move the finished GGUF into place: " + code.message();
        return result;
    }
    result.ok = true;
    result.path = output;
    return result;
}

}  // namespace apogee::models
