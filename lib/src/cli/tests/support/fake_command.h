#pragma once

#include <string>
#include <string_view>

#include "commands/command.h"

namespace apogee::testing {

/// A Command defined entirely in test code.
///
/// Its existence is itself a check on the design: the registry and root
/// command must know nothing about which concrete commands they hold, so a
/// test can supply its own and drive the real registration and parsing paths
/// without depending on whatever the built-in set happens to contain today.
class FakeCommand final : public commands::Command {
public:
    FakeCommand(std::string name, std::string summary);

    [[nodiscard]] std::string_view name() const noexcept override;
    [[nodiscard]] std::string_view summary() const noexcept override;
    void bind(CLI::App& root, const commands::RootContext& context) override;

    /// How many times the bound callback fired.
    [[nodiscard]] int invocations() const noexcept;

    /// The root `--config` value as seen from inside the callback -- i.e. after
    /// parsing, which is the only time a real command may read it.
    [[nodiscard]] const std::string& observed_config_path() const noexcept;

private:
    std::string name_;
    std::string summary_;
    const commands::RootContext* context_{nullptr};
    int invocations_{0};
    std::string observed_config_path_;
};

}  // namespace apogee::testing
