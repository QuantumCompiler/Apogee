#pragma once

#include <filesystem>
#include <string>

namespace apogee::testing {

/// What making a link came to: made, or why not -- said by the test, never a
/// silent pass (57k: `fs_test`'s link cases once checked only `if (!code)`,
/// so on the Windows x64 build, where `create_symlink` always fails, every
/// escape case was skipped and the suite still passed).
struct LinkMade {
    bool made = false;
    /// Why the link could not be made, for a SKIP or a failure.
    std::string why{};
};

/// Makes `link` -- nothing may be there yet -- a directory link to the
/// existing directory `target`: on Windows a junction, set with
/// `FSCTL_SET_REPARSE_POINT`, which needs no privilege (the link the
/// Windows x64 build's library cannot see, and the one a user makes with
/// `mklink /J`); elsewhere a directory symlink. Expected to succeed on every
/// platform: a test REQUIREs it.
[[nodiscard]] LinkMade make_directory_link(const std::filesystem::path& target,
                                           const std::filesystem::path& link);

/// Makes `link` a symbolic link to the file `target`: on Windows through
/// `CreateSymbolicLinkW` with `SYMBOLIC_LINK_FLAG_ALLOW_UNPRIVILEGED_CREATE`,
/// which Windows refuses unless Developer Mode is on or the process is
/// elevated -- then `why` says so, and the test SKIPs saying it; elsewhere
/// `create_symlink`.
[[nodiscard]] LinkMade make_file_link(const std::filesystem::path& target,
                                      const std::filesystem::path& link);

}  // namespace apogee::testing
