#include "tools/environment.h"

#include <array>
#include <cstdlib>

namespace apogee::tools {
namespace {

constexpr std::array<std::string_view, 7> kWeekdays{"Sunday",   "Monday", "Tuesday", "Wednesday",
                                                    "Thursday", "Friday", "Saturday"};

std::string two_digits(int value) {
    return (value < 10 ? "0" : "") + std::to_string(value);
}

std::string_view system_name(platform::OperatingSystem os) {
    switch (os) {
        case platform::OperatingSystem::Linux:
            return "Linux";
        case platform::OperatingSystem::MacOS:
            return "macOS";
        case platform::OperatingSystem::Windows:
            return "Windows";
    }
    return "an unknown system";
}

/// `UTC-06:00`, `UTC+05:30`, `UTC+00:00`.
std::string utc_offset(int minutes) {
    const int magnitude = std::abs(minutes);
    return std::string{"UTC"} + (minutes < 0 ? "-" : "+") + two_digits(magnitude / 60) + ":" +
           two_digits(magnitude % 60);
}

}  // namespace

std::string render_environment_note(const Environment& environment,
                                    const platform::LocalDate& date) {
    std::string note = "Environment:\n";
    note += "- Today is ";
    if (date.weekday >= 0 && date.weekday < static_cast<int>(kWeekdays.size())) {
        note += std::string{kWeekdays.at(static_cast<std::size_t>(date.weekday))} + ", ";
    }
    note += std::to_string(date.year) + "-" + two_digits(date.month) + "-" + two_digits(date.day);
    note += " (time zone ";
    if (!date.zone.empty()) {
        note += date.zone + ", ";
    }
    note += utc_offset(date.utc_offset_minutes) + ").\n";
    note += "- Operating system: " + std::string{system_name(environment.operating_system)} +
            " on " + std::string{platform::to_string(environment.architecture)} + ".\n";
    note += "- Working directory: " + environment.working_directory.string() + ".";
    if (!environment.shell.empty()) {
        note += " Commands run there, through " + environment.shell + ".";
    }
    note += "\n";
    if (!environment.fs_root.empty()) {
        note += "- The file tools reach " + environment.fs_root.string() +
                " and everything under it; a relative path starts there.\n";
    }
    note.pop_back();
    return note;
}

}  // namespace apogee::tools
