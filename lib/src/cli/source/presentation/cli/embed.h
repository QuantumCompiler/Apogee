#pragma once

#include <filesystem>
#include <string>
#include <string_view>

#include "cli/command.h"
#include "operations/collections.h"

/// `apogee embed` — ingest documents and search them.
///
/// **The whole surface works with no model, no API key, and no network.** That
/// is the point of building the lexical floor first: retrieval is available to
/// someone who has installed Apogee and nothing else, and it keeps working
/// when no embedding model can be had — the lexical path never needs one.
namespace apogee::commands {

class EmbedCommand final : public Command {
public:
    [[nodiscard]] std::string_view name() const noexcept override;
    [[nodiscard]] std::string_view summary() const noexcept override;
    void bind(CLI::App& root, const RootContext& context) override;
};

}  // namespace apogee::commands
