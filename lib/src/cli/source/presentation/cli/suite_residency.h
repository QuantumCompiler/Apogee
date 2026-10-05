#pragma once

#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "contracts/config.h"
#include "harness/harness.h"
#include "modelstore/footprint.h"
#include "views/status_line.h"

/// A suite as a set in a session (27e): what selecting one says about fitting
/// this machine, the footprint `models status` and `/suite` show, and the
/// warmup walk on the busy line. The arithmetic is `modelstore/footprint`'s;
/// this is how a person reads it.
namespace apogee::commands {

/// Where the machine's budget is read from: the devices llama.cpp reports
/// (`machine_budget`), or a test's fixed number.
using MachineBudgetSource = std::function<models::MachineBudget()>;

/// The machine's budget as 26a's check reads it, from the devices llama.cpp
/// offloads to; unknown in a build without llama.cpp.
[[nodiscard]] models::MachineBudget machine_budget();

/// `suite` of `config` priced (`models::suite_footprint`), the machine asked
/// only when some member's memory is on it -- a cloud-only suite never wakes
/// a device. A null `machine` leaves the budget unknown.
[[nodiscard]] models::SuiteFootprint price_suite(const harness::Config& config,
                                                 std::string_view suite,
                                                 const MachineBudgetSource& machine);

/// The arithmetic selecting a suite states, on one line:
/// `suite research needs 10600 MiB of this machine's 98304 MiB: root 7468 +
/// helper 2108 + 1024 margin -- fits`. A member whose size is unknown is
/// named as unknown and the sum reads "at least"; a budget that is not known
/// is said so; `forced` marks a suite run over its budget anyway.
[[nodiscard]] std::string admission_line(const models::SuiteFootprint& footprint,
                                         bool forced = false);

/// Why selecting `footprint`'s suite is refused -- its arithmetic, and
/// `way_out` (`--force`, or `/suite <name> --force`) -- or empty when it is
/// not: only a known sum over a known budget refuses.
[[nodiscard]] std::string admission_refusal(const models::SuiteFootprint& footprint,
                                            std::string_view way_out);

/// Whether a backend's model is in memory, as `Harness::resident` answers.
using ResidencyProbe = std::function<std::optional<bool>(std::string_view backend)>;

/// The footprint block: a line per backend the suite puts to use -- its
/// roles, its bytes as weights and cache at its window, or why unknown, and,
/// when `resident` can say, whether its model is in memory now -- then the
/// set's total as the admission line states it. `resident` is a session's
/// (`/suite`); `models status`, which loads nothing, passes none.
[[nodiscard]] std::vector<std::string> footprint_lines(const models::SuiteFootprint& footprint,
                                                       const ResidencyProbe& resident = {});

/// `/suite`'s argument, read (27e): the suite or `off`, then `--force` and
/// `--warm` in any order.
struct SuiteArgument {
    std::string suite;
    bool force = false;
    bool warm = false;
    /// Why it cannot be read, naming the shape; empty when it can.
    std::string error;
};

[[nodiscard]] SuiteArgument parse_suite_argument(std::string_view argument);

/// What the chat banner says of its suite: `  ·  suite research`, and
/// `(over budget, --force)` after it when it was forced (27e).
[[nodiscard]] std::string banner_suite(std::string_view suite, bool forced);

/// The warmup walk over `config`'s active suite on `line` -- `warming suite
/// research: loading helper (2/3)` -- through `Harness::warm`. The line is
/// M1's: silent on a pipe, under `--quiet`, and with JSON output. Returns
/// what to say of each member that could not be loaded.
[[nodiscard]] std::vector<std::string> warm_suite(const harness::Harness& harness,
                                                  const harness::Config& config, BusyLine& line);

/// The label `warm_suite` gives the line for one member.
[[nodiscard]] std::string warm_label(std::string_view suite, std::string_view backend);

}  // namespace apogee::commands
