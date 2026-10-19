#pragma once

#include <string_view>

#include "cli/command.h"

/// `apogee video` -- generating video locally (track 49).
///
/// Since 49c, `video generate "<prompt>" -o <clip>`: one clip from a
/// diffusion pipeline in the store (`--model`, else `models.default_video`),
/// from text or from a seed image (`--image`), at the settings asked within
/// the caps, its seed given or generated and kept. The run is narrated on
/// stderr -- the device and the scale to expect first, then every step --
/// cancellable with Ctrl-C, and it writes nothing at the output until the
/// clip is whole. Its record is the `video.generation` document, printed
/// under `--output-format json` and written beside the clip as its sidecar.
namespace apogee::commands {

class VideoCommand final : public Command {
public:
    [[nodiscard]] std::string_view name() const noexcept override;
    [[nodiscard]] std::string_view summary() const noexcept override;
    void bind(CLI::App& root, const RootContext& context) override;
};

}  // namespace apogee::commands
