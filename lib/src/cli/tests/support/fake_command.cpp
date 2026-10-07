#include "support/fake_command.h"

#include <CLI/CLI.hpp>

#include <utility>

namespace apogee::testing {

FakeCommand::FakeCommand(std::string name, std::string summary)
    : name_{std::move(name)}, summary_{std::move(summary)} {}

std::string_view FakeCommand::name() const noexcept {
    return name_;
}

std::string_view FakeCommand::summary() const noexcept {
    return summary_;
}

void FakeCommand::bind(CLI::App& root, const commands::RootContext& context) {
    context_ = &context;
    CLI::App* cmd = root.add_subcommand(name_, summary_);
    cmd->callback([this]() {
        ++invocations_;
        observed_config_path_ = context_->config_path;
        observed_root_ = harness::current_root();
    });
}

int FakeCommand::invocations() const noexcept {
    return invocations_;
}

const std::string& FakeCommand::observed_config_path() const noexcept {
    return observed_config_path_;
}

const harness::RootResolution& FakeCommand::observed_root() const noexcept {
    return observed_root_;
}

}  // namespace apogee::testing
