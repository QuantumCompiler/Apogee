#pragma once

#include <string_view>

#include "cli/command.h"

/// `apogee review` -- Apogee reviewing merge requests (track 48).
///
/// Since 48b, `review skills list|show`: the lenses a review runs, each a
/// markdown file on the recipes' ladder -- the seven shipped (seeded under
/// `review-skills/`, read from the compiled-in text while no file is there)
/// and any the user drops beside them -- listed with where each comes from
/// and what it applies to, or shown whole; `--output-format json` gives the
/// documents (machine-mode.md). Every skill's answer is held to the one
/// finding contract, `harness::review_finding_schema_text()`.
namespace apogee::commands {

class ReviewCommand final : public Command {
public:
    [[nodiscard]] std::string_view name() const noexcept override;
    [[nodiscard]] std::string_view summary() const noexcept override;
    void bind(CLI::App& root, const RootContext& context) override;
};

}  // namespace apogee::commands
