#pragma once

#include <string_view>

#include "commands/command.h"

/// `apogee knowledge` (alias `kn`) -- the organizational knowledge layer.
///
/// `capture` runs the normalization clerk over a raw conversation -- an
/// argument, a file, a saved chat, or piped stdin -- and stores ONE canonical
/// record: the why behind a decision, what was chosen, whether it shipped,
/// who said it (separately, so it can be stripped), and the artifact it
/// produced. The record lands in an ordinary collection as one chunk whose
/// text is the immutable reasoning and whose metadata is the whole record,
/// with the raw conversation archived privately beside it; `--dry-run` runs
/// everything but the write and says what a real run would do.
///
/// Every path -- this command, chat's `/capture`, the exit-time auto-capture,
/// the HTTP twin -- goes through `commands/knowledge_core.h`, so the record
/// each produces is the same record.
namespace apogee::commands {

class KnowledgeCommand final : public Command {
public:
    [[nodiscard]] std::string_view name() const noexcept override;
    [[nodiscard]] std::string_view summary() const noexcept override;
    void bind(CLI::App& root, const RootContext& context) override;
};

}  // namespace apogee::commands
