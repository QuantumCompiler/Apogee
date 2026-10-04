#pragma once

#include <cstddef>
#include <string>
#include <string_view>

#include "knowledge/clerk.h"
#include "knowledge/record.h"

/// The clerk's revision pass: a client-held draft, a reviewer's instruction,
/// and the raw conversation in; a new draft out. Never a store.
///
/// A revision is a diff, not a fresh capture: the prompt applies the one
/// instruction and carries every other field over verbatim, grounded in the
/// raw conversation and never inventing. It shares the capture schema -- one
/// output shape for both passes -- so a refined draft goes through exactly the
/// finished-record path a captured one does. The loop is a review-UI
/// affordance and lives on the control plane only; a CLI user re-runs
/// `capture --dry-run` (a documented parity skip, Ommi's decision kept).
namespace apogee::knowledge {

/// An instruction is a short review note, not a second conversation: the
/// source material belongs in `raw`. Counted in codepoints.
inline constexpr std::size_t kMaxRefineInstructionLen = 2000;

/// The compiled-in refine prompt, byte-identical to `assets/clerks/refine_prompt.txt`.
[[nodiscard]] std::string_view refine_prompt() noexcept;

/// The refine prompt plus the identical OUTPUT FORMAT block and capture
/// schema the capture pass ends with.
[[nodiscard]] std::string refine_system_prompt();

/// Why `instruction` is unusable -- empty, or over the cap -- or empty when
/// it is fine. Run before any clerk call.
[[nodiscard]] std::string validate_refine_instruction(std::string_view instruction);

/// The user turn: `CURRENT DRAFT` (the six schema fields only, as JSON),
/// `REVIEWER INSTRUCTION`, and `RAW CONVERSATION` -- or an explicit line
/// saying none was supplied, so the model knows not to invent grounding.
[[nodiscard]] std::string refine_user_message(const Record& draft, std::string_view instruction,
                                              std::string_view raw);

/// One bounded revision pass. The instruction guards fire before the clerk
/// is called; a non-conforming answer is an error, never a partial record;
/// `supersedes` is carried through and a dropped `source` keeps the draft's.
[[nodiscard]] Draft run_refine(const ClerkFn& clerk, const Record& draft,
                               std::string_view instruction, std::string_view raw);

}  // namespace apogee::knowledge
