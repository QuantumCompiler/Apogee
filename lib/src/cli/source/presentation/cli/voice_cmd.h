#pragma once

#include <string_view>

#include "cli/command.h"

/// `apogee voice` -- speech in the box (track 50).
///
/// Since 50a, `voice say "<text>" -o <file.wav>`: the text spoken by a voice
/// model in the store (`--model`, else the store's one), in one of its own
/// voices (`--voice`, completed from them), into one WAV written only when
/// whole, each segment's duration measured from the file. Its record is the
/// `voice.speech` document, printed under `--output-format json` and
/// written beside the file.
namespace apogee::commands {

class VoiceCommand final : public Command {
public:
    [[nodiscard]] std::string_view name() const noexcept override;
    [[nodiscard]] std::string_view summary() const noexcept override;
    void bind(CLI::App& root, const RootContext& context) override;
};

}  // namespace apogee::commands
