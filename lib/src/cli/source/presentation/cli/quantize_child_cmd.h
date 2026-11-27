#pragma once

#include <string_view>

#include "cli/command.h"

/// `apogee __quantize <input> <output> <type>` -- one quantization run as a
/// child of `models quantize` and the register chain (57i), so a stop
/// terminates it at once: llama.cpp's quantizer takes no abort hook, and
/// the GUI's Stop turns into SIGTERM ten seconds after its Ctrl-C.
///
/// A protocol, not a feature: hidden from `--help` like `__complete` and
/// `__machine-schema`. stdout carries `models::quantize_for_parent`'s lines
/// -- each tensor done, then the result -- and nothing else; llama.cpp's log
/// stays off it as `models quantize` keeps it off the terminal (M3), and
/// whatever reaches stderr is the parent's bounded tail. It takes no config
/// and no home: the paths it is handed are the parent's staging.
namespace apogee::commands {

class QuantizeChildCommand final : public Command {
public:
    [[nodiscard]] std::string_view name() const noexcept override;
    [[nodiscard]] std::string_view summary() const noexcept override;
    void bind(CLI::App& root, const RootContext& context) override;
};

}  // namespace apogee::commands
