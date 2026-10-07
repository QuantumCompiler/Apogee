#include "cli/root.h"

#include <CLI/CLI.hpp>

#include <cstdlib>
#include <iostream>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "cli/version_command.h"
#include "contracts/paths.h"

namespace apogee::commands {

namespace {
constexpr auto kDescription =
    "Apogee -- run local and cloud LLMs from one harness.\n"
    "\n"
    "Cloud backends (Anthropic, OpenAI, Google, Ollama) and local inference via\n"
    "llama.cpp sit behind one interface, so which model runs is configuration\n"
    "rather than architecture.";

/// The variable CLI11 reads `--config` from when the flag is absent.
constexpr auto kConfigEnvVar = "APOGEE_CONFIG";

[[nodiscard]] std::string channel_help(harness::Channel channel) {
    return "Use the " + std::string{harness::channel_name(channel)} +
           " channel's data directory, ~/" + std::string{harness::channel_directory(channel)} +
           ", for this run only: the whole layout, never saved (uninstall ignores it)";
}

/// Refuses the run before any command starts, saying why.
[[noreturn]] void refuse(const std::string& why) {
    std::cerr << "apogee: " << why << "\n";
    throw CLI::RuntimeError(1);
}

}  // namespace

RootCommand::RootCommand(CommandRegistry registry)
    : registry_{std::move(registry)}, app_{std::make_unique<CLI::App>(kDescription, "apogee")} {
    // A plain flag, answered in apply_root_flags(), rather than CLI11's
    // set_version_flag: that one is answered while the flags are still being
    // read, before a root flag is in force -- and `--dev --version` must name
    // the dev root.
    version_option_ = app_->add_flag("-V,--version", "Print version information and exit");

    // Deliberately NOT ->check(CLI::ExistingFile): `apogee config init --config
    // <new path>` has to name a file that does not exist yet. A missing config
    // is reported by the config engine, whose message names the fix ("run
    // 'apogee config init'") rather than CLI11's generic one.
    config_option_ = app_->add_option("--config", context_.config_path,
                                      "Path to the config file (default: config/config.yaml in "
                                      "the data directory). Names a file only -- the data "
                                      "directory stays put; --custom names both")
                         ->type_name(kPathValue)
                         ->envname(kConfigEnvVar);

    // The root flags (M10): the first rung of the one chain that chooses the
    // data directory (contracts/paths.h) -- a flag, then APOGEE_HOME, then the
    // channel baked into this build. At most one, and only for this run.
    std::vector<CLI::Option*> root_flags;
    for (std::size_t i = 0; i < harness::kChannels.size(); ++i) {
        const harness::Channel channel = harness::kChannels.at(i);
        const std::string help = channel_help(channel);
        channel_options_.at(i) = app_->add_flag(std::string{harness::channel_flag(channel)}, help);
        root_flags.push_back(channel_options_.at(i));
    }
    custom_option_ =
        app_->add_option(std::string{harness::kCustomFlag}, custom_config_,
                         "Use the install a config file sits in, for this run only: "
                         "<root>/config/<file> roots the whole layout at <root> and reads that "
                         "file (uninstall ignores it)")
            ->type_name(kPathValue);
    root_flags.push_back(custom_option_);
    for (std::size_t i = 0; i < root_flags.size(); ++i) {
        for (std::size_t j = i + 1; j < root_flags.size(); ++j) {
            root_flags.at(i)->excludes(root_flags.at(j));  // both ways: CLI11 adds the reverse
        }
    }

    // After the parse and its checks, before any command's callback.
    app_->parse_complete_callback([this]() { apply_root_flags(); });

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

void RootCommand::apply_root_flags() {
    // A parse of its own: an earlier run's flag is that run's, and is put
    // back before this one decides anything.
    root_scope_.reset();

    std::optional<harness::RootFlag> flag;
    for (std::size_t i = 0; i < harness::kChannels.size(); ++i) {
        if (channel_options_.at(i)->count() > 0) {
            flag = harness::RootFlag{.channel = harness::kChannels.at(i), .custom_config = {}};
        }
    }
    if (custom_option_->count() > 0) {
        flag = harness::RootFlag{.channel = std::nullopt, .custom_config = custom_config_};
    }

    if (flag.has_value()) {
        harness::RootInputs inputs = harness::current_root_inputs();
        inputs.flag = flag;
        const harness::RootResolution resolved = harness::resolve_root(inputs);
        if (!resolved.ok()) {
            refuse(resolved.error);
        }
        // `--custom` names the config file too, so a `--config` naming
        // another one is a second answer to the same question: refused, not
        // guessed. A channel flag claims no file, so `--config` beside it reads
        // that file over the channel's data directory, as it does beside
        // APOGEE_HOME.
        if (!flag->channel.has_value() && config_option_->count() > 0 &&
            !harness::same_path(resolved.config, context_.config_path)) {
            const char* from_env = std::getenv(kConfigEnvVar);
            const std::string source = from_env != nullptr && context_.config_path == from_env
                                           ? std::string{kConfigEnvVar}
                                           : std::string{"--config"};
            refuse(flag->spelling() + " reads " + resolved.config.string() + ", but " + source +
                   " names " + context_.config_path +
                   " -- a run reads one config; drop one of them");
        }
        root_scope_ = std::make_unique<harness::RootFlagScope>(std::move(flag));
    }

    if (version_option_->count() > 0) {
        throw CLI::CallForVersion(version_report(context_), 0);
    }
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
