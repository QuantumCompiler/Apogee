#pragma once

#include <filesystem>

namespace apogee::testing {

/// Sets a file's or a directory's modification time.
///
/// `std::filesystem::last_write_time` does both on POSIX, and through libc++
/// on Windows -- but MinGW's libstdc++ sets a time through `_wutime`, which
/// cannot open a directory, and throws "cannot set file time: Permission
/// denied". That failed every test that ages a directory on the x64 Windows
/// job (2026-09-25). There, when the standard call fails, the directory's
/// time is set through the Win32 API instead, with the FILETIME borrowed from
/// a scratch file the standard call CAN stamp, so no clock epoch is converted
/// by hand.
///
/// Nothing in Apogee itself sets a time; only tests age what they build.
void set_modified_time(const std::filesystem::path& path, std::filesystem::file_time_type time);

}  // namespace apogee::testing
