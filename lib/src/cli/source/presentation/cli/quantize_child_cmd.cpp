#include "cli/quantize_child_cmd.h"

#include <CLI/CLI.hpp>

#include <iostream>
#include <memory>
#include <string>

#include "models/quantize.h"

namespace apogee::commands {

std::string_view QuantizeChildCommand::name() const noexcept {
    return models::kQuantizeVerb;
}

std::string_view QuantizeChildCommand::summary() const noexcept {
    return "Quantize one GGUF for the apogee that started this one";
}

void QuantizeChildCommand::bind(CLI::App& root, const RootContext& /*context*/) {
    CLI::App* cmd = root.add_subcommand(std::string{name()}, std::string{summary()});
    cmd->group("");  // hidden from --help: a protocol its parent speaks
    auto input = std::make_shared<std::string>();
    auto output = std::make_shared<std::string>();
    auto type = std::make_shared<std::string>();
    cmd->add_option("input", *input, "The GGUF to quantize")->required();
    cmd->add_option("output", *output, "Where the quantized GGUF is written")->required();
    cmd->add_option("type", *type, "The quantization type")->required();
    cmd->callback([input, output, type]() {
        if (const int code = models::quantize_for_parent(*input, *output, *type, std::cout);
            code != 0) {
            throw CLI::RuntimeError(code);
        }
    });
}

}  // namespace apogee::commands
