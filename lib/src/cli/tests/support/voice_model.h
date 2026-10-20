#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include "support/mlx_model.h"

/// Voice model directories built for tests (50a), laid out as Kokoro-82M's
/// repository is: `config.json`, one checkpoint (`kokoro-v1_0.pth`) and a
/// `voices/` directory of packs. Each `.pth`/`.pt` is a few bytes shaped as
/// a PyTorch save is -- a zip archive's local header first and its end
/// record last -- so the reader's wholeness check has something real to
/// read. Never weights; built, not committed, for the broken cases' sake.
namespace apogee::testing {

struct VoiceModelSpec {
    std::vector<std::string> voices{"af_heart", "am_adam", "bf_emma"};
    std::string checkpoint = "kokoro-v1_0.pth";
};

/// A zip-shaped file `size` bytes long: what the reader takes for whole.
[[nodiscard]] inline std::string zip_bytes(std::size_t size = 96) {
    std::string bytes{"PK\x03\x04", 4};
    bytes += std::string(size > 30 ? size - 30 : 0, 'w');
    bytes += std::string{"PK\x05\x06", 4};
    bytes += std::string(22, '\0');
    return bytes;
}

/// A whole voice model at `dir`.
inline void write_voice_model(const std::filesystem::path& dir, const VoiceModelSpec& spec = {}) {
    write_fixture_file(dir / "config.json",
                       "{\"istftnet\": {\"upsample_rates\": [10, 6]}, \"plbert\": {}, "
                       "\"vocab\": {\"a\": 43}}\n");
    write_fixture_file(dir / spec.checkpoint, zip_bytes(256));
    for (const std::string& voice : spec.voices) {
        write_fixture_file(dir / "voices" / (voice + ".pt"), zip_bytes());
    }
}

}  // namespace apogee::testing
