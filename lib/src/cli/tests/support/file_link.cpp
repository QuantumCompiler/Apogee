#include "support/file_link.h"

#include <system_error>

#if defined(_WIN32)
#include <windows.h>
#include <winioctl.h>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>
#endif

namespace apogee::testing {

#if defined(_WIN32)
namespace {

[[nodiscard]] std::string windows_message(DWORD error) {
    return std::error_code{static_cast<int>(error), std::system_category()}.message();
}

/// Appends `value`'s bytes, little-endian as Windows lays them out.
template <typename T>
void put(std::vector<std::byte>& buffer, T value) {
    const std::size_t at = buffer.size();
    buffer.resize(at + sizeof value);
    std::memcpy(buffer.data() + at, &value, sizeof value);
}

/// Appends `text`'s UTF-16 units and its terminator.
void put_text(std::vector<std::byte>& buffer, const std::wstring& text) {
    for (const wchar_t unit : text) {
        put(buffer, static_cast<std::uint16_t>(unit));
    }
    put(buffer, std::uint16_t{0});
}

}  // namespace
#endif

LinkMade make_directory_link(const std::filesystem::path& target,
                             const std::filesystem::path& link) {
    LinkMade out;
#if defined(_WIN32)
    std::error_code code;
    const std::wstring absolute = std::filesystem::absolute(target, code).wstring();
    if (code) {
        out.why = "the target cannot be made absolute: " + code.message();
        return out;
    }
    if (CreateDirectoryW(link.c_str(), nullptr) == 0) {
        out.why = "the junction's folder cannot be made: " + windows_message(GetLastError());
        return out;
    }
    // A mount point's reparse data (REPARSE_DATA_BUFFER's
    // MountPointReparseBuffer): the NT name `\??\C:\…` to follow, then the
    // name to print, each with its terminator.
    const std::wstring substitute = L"\\??\\" + absolute;
    const auto substitute_bytes = static_cast<std::uint16_t>(substitute.size() * sizeof(wchar_t));
    const auto print_bytes = static_cast<std::uint16_t>(absolute.size() * sizeof(wchar_t));
    const auto data_length =
        static_cast<std::uint16_t>(4 * sizeof(std::uint16_t) + substitute_bytes + sizeof(wchar_t) +
                                   print_bytes + sizeof(wchar_t));
    std::vector<std::byte> buffer;
    put(buffer, static_cast<std::uint32_t>(IO_REPARSE_TAG_MOUNT_POINT));
    put(buffer, data_length);
    put(buffer, std::uint16_t{0});  // reserved
    put(buffer, std::uint16_t{0});  // the substitute name's offset
    put(buffer, substitute_bytes);
    put(buffer, static_cast<std::uint16_t>(substitute_bytes + sizeof(wchar_t)));
    put(buffer, print_bytes);
    put_text(buffer, substitute);
    put_text(buffer, absolute);

    const HANDLE handle =
        CreateFileW(link.c_str(), GENERIC_WRITE, 0, nullptr, OPEN_EXISTING,
                    FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_BACKUP_SEMANTICS, nullptr);
    if (handle == INVALID_HANDLE_VALUE) {
        out.why = "the junction's folder cannot be opened: " + windows_message(GetLastError());
        (void)RemoveDirectoryW(link.c_str());
        return out;
    }
    DWORD returned = 0;
    const BOOL set =
        DeviceIoControl(handle, FSCTL_SET_REPARSE_POINT, buffer.data(),
                        static_cast<DWORD>(buffer.size()), nullptr, 0, &returned, nullptr);
    const DWORD error = GetLastError();
    CloseHandle(handle);
    if (set == 0) {
        out.why = "Windows refused the junction: " + windows_message(error);
        (void)RemoveDirectoryW(link.c_str());
        return out;
    }
    out.made = true;
#else
    std::error_code code;
    std::filesystem::create_directory_symlink(target, link, code);
    if (code) {
        out.why = "the directory symlink cannot be made: " + code.message();
        return out;
    }
    out.made = true;
#endif
    return out;
}

LinkMade make_file_link(const std::filesystem::path& target, const std::filesystem::path& link) {
    LinkMade out;
#if defined(_WIN32)
    if (CreateSymbolicLinkW(link.c_str(), target.c_str(),
                            SYMBOLIC_LINK_FLAG_ALLOW_UNPRIVILEGED_CREATE) == 0) {
        out.why =
            "Windows will not make a file symbolic link for this process (Developer Mode is off "
            "and it is not elevated): " +
            windows_message(GetLastError());
        return out;
    }
    out.made = true;
#else
    std::error_code code;
    std::filesystem::create_symlink(target, link, code);
    if (code) {
        out.why = "the symlink cannot be made: " + code.message();
        return out;
    }
    out.made = true;
#endif
    return out;
}

}  // namespace apogee::testing
