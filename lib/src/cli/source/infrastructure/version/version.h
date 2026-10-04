#pragma once

#include <string>
#include <string_view>

namespace apogee::version {

/// Semantic version of this build, e.g. "0.1.0". Set by the top-level
/// project() call and stamped in at configure time.
[[nodiscard]] std::string_view semantic();

/// Short git commit this build came from, or "unknown" when built from a
/// source export with no repository.
[[nodiscard]] std::string_view git_commit();

/// UTC date the build was configured, "YYYY-MM-DD".
[[nodiscard]] std::string_view build_date();

/// The single line `apogee --version` prints, e.g.
/// "apogee 0.1.0 (a1b2c3d, 2026-08-24, macos-arm64)".
[[nodiscard]] std::string full();

}  // namespace apogee::version
