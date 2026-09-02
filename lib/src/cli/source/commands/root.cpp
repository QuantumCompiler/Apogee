#include "commands/root.h"

#include <CLI/CLI.hpp>

#include <iostream>
#include <string>
#include <utility>

#include "version/version.h"

namespace apogee::commands {

namespace {
constexpr auto kDescription =
    "Apogee -- run local and cloud LLMs from one harness.\n"
    "\n"
    "Cloud backends (Anthropic, OpenAI, Google, Ollama) and local inference via\n"
    "llama.cpp sit behind one interface, so which model runs is configuration\n"
    "rather than architecture.";
}  // namespace

RootCommand::RootCommand(CommandRegistry registry)
    : registry_{std::move(registry)}, app_{std::make_unique<CLI::App>(kDescription, "apogee")} {
    app_->set_version_flag("-V,--version", version::full(), "Print version information and exit");

    // Deliberately NOT ->check(CLI::ExistingFile): `apogee config init --config
    // <new path>` has to name a file that does not exist yet. A missing config
    // is reported by the config engine, whose message names the fix ("run
    // 'apogee config init'") rather than CLI11's generic one.
    app_->add_option("--config", context_.config_path,
                     "Path to the config file (default: ~/.apogee/config/config.yaml)")
        ->envname("APOGEE_CONFIG");

    // At most one subcommand per invocation; zero is legal and prints help
    // (handled in run(), so that `apogee` with no arguments is a success, not
    // a usage error).
    app_->require_subcommand(0, 1);

    // Populated BEFORE binding: the completion protocol reads it from the
    // context it is handed, so it has to be complete by then.
    for (const std::string_view name : registry_.names()) {
        context_.command_names.emplace_back(name);
    }

    registry_.bind_all(*app_, context_);
}

RootCommand::~RootCommand() = default;

CLI::App& RootCommand::app() noexcept {
    return *app_;
}

const CLI::App& RootCommand::app() const noexcept {
    return *app_;
}

const RootContext& RootCommand::context() const noexcept {
    return context_;
}

int RootCommand::run(int argc, const char* const* argv) {
    if (argc <= 1) {
        std::cout << app_->help();
        return 0;
    }

    try {
        app_->parse(argc, argv);
    } catch (const CLI::ParseError& e) {
        // Covers --help and --version (exit 0) as well as usage errors and any
        // CLI::RuntimeError a command threw to set its own exit code.
        return app_->exit(e);
    }

    return 0;
}

}  // namespace apogee::commands
