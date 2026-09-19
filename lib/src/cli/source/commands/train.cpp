#include "commands/train.h"

#include <CLI/CLI.hpp>

#include <iostream>
#include <memory>
#include <string>

#include "commands/helpers.h"
#include "harness/layout.h"
#include "harness/paths.h"
#include "platform/child_process.h"
#include "platform/platform.h"

namespace apogee::commands {
namespace {

[[noreturn]] void fail_user(const std::string& message) {
    std::cerr << "apogee train: " << message << "\n";
    throw CLI::RuntimeError(kUserError);
}

[[nodiscard]] harness::Config load_config_or_default(const RootContext& context) {
    try {
        const std::filesystem::path path = harness::resolve_config_path(context.config_path);
        std::error_code code;
        if (!std::filesystem::exists(path, code)) {
            return harness::Config{};
        }
        return harness::load_config(path);
    } catch (const harness::ConfigError& e) {
        fail_user(e.what());
    }
}

std::string describe_status(const training::PythonEnvStatus& status) {
    if (!status.exists) {
        return "not created";
    }
    std::string sets;
    for (const std::string& set : status.sets) {
        sets += sets.empty() ? set : ", " + set;
    }
    return "at " + status.interpreter.parent_path().parent_path().string() +
           (sets.empty() ? std::string{"; no requirement sets installed"} : "; sets: " + sets);
}

}  // namespace

std::string detect_trainer(std::string& reason) {
    reason.clear();
#if defined(__APPLE__) && defined(__aarch64__)
    return "mlx";
#else
    if (!platform::find_on_path("nvidia-smi").empty()) {
        return "peft";
    }
    reason = "not Apple Silicon, and nvidia-smi is not on PATH -- no trainer stack fits this host";
    return {};
#endif
}

training::PythonEnv require_python_env(const harness::Config& config, std::string_view purpose) {
    training::PythonEnv env{harness::training_venv_dir()};
    if (env.exists()) {
        return env;
    }
    const bool interactive =
        platform::is_terminal(platform::StandardStream::In) && !stdin_is_piped();
    if (!interactive) {
        std::cerr << "apogee " << purpose
                  << ": the Python environment has not been created yet. Run 'apogee train "
                     "setup' first (it creates "
                  << env.dir().string() << ", never touching the system Python)\n";
        throw CLI::RuntimeError(kUserError);
    }
    std::cerr << purpose << " needs a Python environment, which Apogee keeps under\n  "
              << env.dir().string() << "\n(never the system Python). Create it now? [y/N] "
              << std::flush;
    std::string answer;
    std::getline(std::cin, answer);
    if (answer != "y" && answer != "Y" && answer != "yes") {
        fail_user("not created. Run 'apogee train setup' when ready");
    }
    std::string error;
    const std::filesystem::path base = training::locate_base_python(config.training.python, error);
    if (base.empty()) {
        fail_user(error);
    }
    std::cerr << "creating the Python environment from " << base.string() << " ...\n";
    if (const std::string failure = env.create(base); !failure.empty()) {
        fail_user(failure);
    }
    return env;
}

void run_train_setup(const harness::Config& config, const SetupRequest& request) {
    training::PythonEnv env{harness::training_venv_dir()};
    std::vector<training::RequirementSet> sets = request.with;
    if (!request.trainer.empty()) {
        std::string trainer = request.trainer;
        if (trainer == "auto") {
            std::string reason;
            trainer = detect_trainer(reason);
            if (trainer.empty()) {
                std::cout << "no trainer stack selected: " << reason << "\n";
            } else {
                std::cout << "trainer stack for this host: " << trainer << "\n";
            }
        }
        if (trainer == "mlx") {
            sets.push_back(training::RequirementSet::Mlx);
        } else if (trainer == "peft") {
            sets.push_back(training::RequirementSet::Peft);
        } else if (!trainer.empty()) {
            fail_user("--trainer must be auto, mlx or peft (got '" + trainer + "')");
        }
    }

    if (!env.exists()) {
        std::string error;
        const std::filesystem::path base =
            training::locate_base_python(config.training.python, error);
        if (base.empty()) {
            fail_user(error);
        }
        std::cout << "creating " << env.dir().string() << " from " << base.string() << " ...\n";
        if (const std::string failure = env.create(base); !failure.empty()) {
            fail_user(failure);
        }
        std::cout << "created\n";
    } else {
        std::cout << "environment already exists at " << env.dir().string() << "\n";
    }

    for (const training::RequirementSet set : sets) {
        std::cout << "installing the '" << training::to_string(set) << "' set:";
        for (const std::string_view package : training::packages_for(set)) {
            std::cout << " " << package;
        }
        std::cout << " ...\n" << std::flush;
        if (const std::string failure = env.install(set); !failure.empty()) {
            fail_user(failure);
        }
        std::cout << "installed\n";
    }

    std::cout << "\npython env: " << describe_status(env.status()) << "\n";
    if (sets.empty()) {
        std::cout << "\nNothing installed yet. Add a requirement set when a command needs it:\n"
                  << "  apogee train setup --with prepare     # Parquet in 'datasets prepare'\n"
                  << "  apogee train setup --trainer auto     # the trainer stack for this host\n";
    }
}

std::string_view TrainCommand::name() const noexcept {
    return "train";
}

std::string_view TrainCommand::summary() const noexcept {
    return "Fine-tune local models: the Python environment (setup)";
}

void TrainCommand::bind(CLI::App& root, const RootContext& context) {
    CLI::App* cmd = root.add_subcommand(std::string{name()}, std::string{summary()});
    cmd->require_subcommand(1);

    auto trainer = std::make_shared<std::string>();
    auto with = std::make_shared<std::vector<std::string>>();
    CLI::App* setup = cmd->add_subcommand(
        "setup", "Create the Python environment under training/venv and install requirement sets");
    setup->add_option("--trainer", *trainer,
                      "Install a trainer's stack: auto (detect this host), mlx, or peft");
    setup->add_option("--with", *with,
                      "Install a requirement set: prepare (Parquet), mlx, peft, convert");
    setup->callback([&context, trainer, with]() {
        const harness::Config config = load_config_or_default(context);
        SetupRequest request;
        request.trainer = *trainer;
        for (const std::string& name : *with) {
            const std::optional<training::RequirementSet> set =
                training::requirement_set_from_string(name);
            if (!set.has_value()) {
                std::string names;
                for (const std::string_view known : training::requirement_set_names()) {
                    names += names.empty() ? std::string{known} : ", " + std::string{known};
                }
                fail_user("unknown requirement set '" + name + "' (want one of: " + names + ")");
            }
            request.with.push_back(*set);
        }
        run_train_setup(config, request);
    });
}

}  // namespace apogee::commands
