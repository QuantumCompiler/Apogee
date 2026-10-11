#pragma once

#include <nlohmann/json_fwd.hpp>

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#include "cli/command.h"
#include "contracts/config.h"
#include "harness/harness.h"
#include "operations/dataset_core.h"
#include "training/datasets.h"
#include "training/kit.h"
#include "training/synth.h"

/// `apogee datasets` -- everything a fine-tuning run consumes.
///
/// `prepare` converts a local file through the shipped `prepare_dataset.py`
/// under the environment's interpreter; `create` scaffolds one from a
/// template, from nothing, or from the user's own chat sessions; `synth`
/// distils one from a teacher through the model-free synth core -- the
/// teacher named explicitly, a direct API or local call, batches in flight
/// with retries. `kits`, `list`, `info` and `delete` round it out, and
/// `pull` brings a Hugging Face dataset down through the models item's
/// ladder for `prepare` to convert.
///
/// The cores below are shared with the admin twins, which is what makes a
/// dataset created over HTTP byte-identical to one created here.
namespace apogee::commands {

/// A dataset's size as `datasets list` words it: `812 B`, `14 KiB`.
[[nodiscard]] std::string human_size(std::int64_t bytes);

/// `datasets info <name>` as a person reads it (37f): the Datasets view's
/// card.
[[nodiscard]] std::string dataset_info_text(const training::DatasetInfo& info);

/// `datasets kits` as a person reads it (37f): the installed training kits,
/// or where none were found.
[[nodiscard]] std::string dataset_kits_text();

class DatasetsCommand final : public Command {
public:
    [[nodiscard]] std::string_view name() const noexcept override;
    [[nodiscard]] std::string_view summary() const noexcept override;
    void bind(CLI::App& root, const RootContext& context) override;
};

}  // namespace apogee::commands
