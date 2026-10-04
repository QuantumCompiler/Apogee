#include "tools/environment.h"

#include <array>
#include <cstdlib>

#include "agent/fetch_url.h"
#include "agent/web_search.h"

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

ToolReach tool_reach(const agent::ToolRegistry& registry) {
    return ToolReach{.search = registry.find(agent::kWebSearchToolName) != nullptr,
                     .read_pages = registry.find(agent::kFetchUrlToolName) != nullptr};
}

std::string render_tool_use_policy(const ToolReach& reach) {
    if (!reach.search && !reach.read_pages) {
        return {};
    }
    std::string policy = "How to use these tools: when a question turns on ";
    if (reach.search) {
        policy +=
            "something current, recent or beyond what you can know -- the weather, news, "
            "prices, scores, schedules";
        policy += reach.read_pages ? ", what a web page says now -- search the web and read the "
                                     "pages you find"
                                   : " -- search the web";
    } else {
        policy +=
            "what a web page says now -- a link the user gives you, or a page whose address you "
            "know -- read the page";
    }
    policy +=
        " before you answer, rather than answering from memory or saying you cannot. Never say "
        "you lack access to information one of your tools can get. When you already know the "
        "answer, just answer.";
    return policy;
}

}  // namespace apogee::tools
