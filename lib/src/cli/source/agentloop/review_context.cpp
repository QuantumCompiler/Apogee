#include "agentloop/review_context.h"

#include <cctype>

namespace apogee::agentloop {
namespace {

std::string trim(std::string_view text) {
    std::size_t begin = 0;
    while (begin < text.size() && std::isspace(static_cast<unsigned char>(text[begin])) != 0) {
        ++begin;
    }
    std::size_t end = text.size();
    while (end > begin && std::isspace(static_cast<unsigned char>(text[end - 1])) != 0) {
        --end;
    }
    return std::string{text.substr(begin, end - begin)};
}

std::string lower(std::string text) {
    for (char& c : text) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return text;
}

}  // namespace

bool valid_fetch_mode(std::string_view mode) noexcept {
    return mode == "auto" || mode == "always" || mode == "never";
}

ReviewContext parse_branch_arg(std::string_view arg, const ReviewContext& current) {
    ReviewContext next = current;
    const std::string text = trim(arg);
    const std::string word = lower(text);
    if (word == "off" || word == "clear" || word == "none") {
        next.head.clear();
        next.base.clear();
        return next;
    }
    std::size_t at = text.find("...");
    std::size_t width = 3;
    if (at == std::string::npos) {
        at = text.find("..");
        width = 2;
    }
    if (at != std::string::npos) {
        next.base = trim(text.substr(0, at));
        next.head = trim(text.substr(at + width));
        return next;
    }
    next.head = text;
    return next;
}

std::string review_note(const ReviewContext& context) {
    if (!context.active()) {
        return {};
    }
    const std::string head = context.head.empty() ? "the current branch" : context.head;
    const std::string base =
        context.base.empty() ? "the repository's default branch" : context.base;
    const std::string remote = context.remote.empty() ? "origin" : context.remote;
    std::string spec;
    if (!context.head.empty()) {
        spec = " (e.g. `git diff " + (context.base.empty() ? "<default-branch>" : context.base) +
               "..." + context.head + "`)";
    }
    return "Git review context: review " + head + " compared to " + base +
           " WITHOUT checking it out" + spec +
           ". The git tools already default to that comparison -- call git_diff and git_log "
           "with no refs. Remote: " +
           remote + ".";
}

std::string review_summary(const ReviewContext& context) {
    const std::string head = context.head.empty() ? "current branch" : context.head;
    const std::string base = context.base.empty() ? "default branch" : context.base;
    const std::string remote = context.remote.empty() ? "origin" : context.remote;
    return head + " vs " + base + " (remote " + remote + ")";
}

std::string compose_system_prompt(std::string_view prompt, std::string_view note) {
    const std::string first = trim(prompt);
    const std::string second = trim(note);
    if (first.empty()) {
        return second;
    }
    if (second.empty()) {
        return first;
    }
    return first + "\n\n" + second;
}

}  // namespace apogee::agentloop
