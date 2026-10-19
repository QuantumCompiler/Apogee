#include "cli/review_cmd.h"

#include <CLI/CLI.hpp>

#include <algorithm>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

#include "cli/helpers.h"
#include "contracts/layout.h"
#include "contracts/review_skill.h"
#include "machine/json_reporter.h"

namespace apogee::commands {
namespace {

[[noreturn]] void fail(const std::string& message) {
    std::cerr << "apogee review: " << message << "\n";
    throw CLI::RuntimeError(1);
}

/// What a skill applies to, in a few words: `every file`, or its hints.
[[nodiscard]] std::string applies_summary(const harness::ReviewSkillApplies& applies) {
    if (applies.everywhere()) {
        return "every file";
    }
    std::vector<std::string> parts;
    if (!applies.extensions.empty()) {
        parts.push_back(std::to_string(applies.extensions.size()) + " extension" +
                        (applies.extensions.size() == 1 ? "" : "s"));
    }
    if (!applies.paths.empty()) {
        parts.push_back(std::to_string(applies.paths.size()) + " path" +
                        (applies.paths.size() == 1 ? "" : "s"));
    }
    if (!applies.languages.empty()) {
        parts.push_back(std::to_string(applies.languages.size()) + " language" +
                        (applies.languages.size() == 1 ? "" : "s"));
    }
    std::string out;
    for (const std::string& part : parts) {
        out += (out.empty() ? "" : ", ") + part;
    }
    return out;
}

[[nodiscard]] std::string joined(const std::vector<std::string>& items) {
    std::string out;
    for (const std::string& item : items) {
        out += (out.empty() ? "" : ", ") + item;
    }
    return out;
}

void list_skills(ReadFormat format) {
    const harness::ReviewSkillBook book = harness::review_skill_book(harness::review_skills_dir());
    if (format == ReadFormat::Json) {
        write_document(std::cout, harness::to_json(book));
        return;
    }
    for (const std::string& problem : book.problems) {
        std::cerr << "[review] skipped: " << problem << "\n";
    }
    if (book.skills.empty()) {
        std::cout << "no review skills -- 'apogee check --fix' seeds the shipped ones\n";
        return;
    }
    std::size_t width = 4;
    std::size_t applies_width = 10;
    for (const harness::ReviewSkillEntry& entry : book.skills) {
        width = std::max(width, entry.skill.name.size());
        applies_width = std::max(applies_width, applies_summary(entry.skill.applies).size());
    }
    const auto row = [&](std::string_view name, std::string_view source, std::string_view applies,
                         std::string_view description) {
        std::cout << name << std::string(width + 2 - name.size(), ' ') << source
                  << std::string(9 - source.size(), ' ') << applies
                  << std::string(applies_width + 2 - applies.size(), ' ') << description << "\n";
    };
    row("NAME", "SOURCE", "APPLIES TO", "DESCRIPTION");
    for (const harness::ReviewSkillEntry& entry : book.skills) {
        row(entry.skill.name, harness::to_string(entry.source),
            applies_summary(entry.skill.applies), entry.skill.description);
    }
}

void show_skill(const std::string& name, ReadFormat format) {
    const harness::ReviewSkillBook book = harness::review_skill_book(harness::review_skills_dir());
    const harness::ReviewSkillEntry* entry = book.find(name);
    if (entry == nullptr) {
        fail("no review skill named '" + name + "'" +
             (book.skills.empty() ? std::string{} : " (there are: " + book.names() + ")"));
    }
    if (format == ReadFormat::Json) {
        write_document(std::cout, harness::to_json(*entry));
        return;
    }
    const harness::ReviewSkill& skill = entry->skill;
    std::cout << skill.name << "  " << harness::to_string(entry->source)
              << (entry->path.empty() ? std::string{} : "  " + entry->path.string()) << "\n"
              << skill.description << "\n\n";
    if (skill.applies.everywhere()) {
        std::cout << "applies to: every file\n";
    } else {
        if (!skill.applies.extensions.empty()) {
            std::cout << "extensions: " << joined(skill.applies.extensions) << "\n";
        }
        if (!skill.applies.paths.empty()) {
            std::cout << "paths:      " << joined(skill.applies.paths) << "\n";
        }
        if (!skill.applies.languages.empty()) {
            std::cout << "languages:  " << joined(skill.applies.languages) << "\n";
        }
    }
    std::cout << "\n" << skill.prompt << "\n";
}

}  // namespace

std::string_view ReviewCommand::name() const noexcept {
    return "review";
}

std::string_view ReviewCommand::summary() const noexcept {
    return "Review merge requests: the lenses a review runs (skills)";
}

void ReviewCommand::bind(CLI::App& root, const RootContext& /*context*/) {
    CLI::App* cmd = root.add_subcommand(std::string{name()}, std::string{summary()});
    cmd->require_subcommand(1);

    CLI::App* skills = cmd->add_subcommand(
        "skills",
        "The review skills -- markdown lenses, shipped and your own, each held to one finding "
        "contract");
    skills->require_subcommand(1);

    auto list_format = std::make_shared<ReadFormat>(ReadFormat::Text);
    CLI::App* list = skills->add_subcommand(
        "list", "Every review skill: where it comes from, what it applies to, what it hunts");
    add_read_format(list, list_format);
    list->callback([list_format]() { list_skills(*list_format); });

    auto skill = std::make_shared<std::string>();
    auto show_format = std::make_shared<ReadFormat>(ReadFormat::Text);
    CLI::App* show =
        skills->add_subcommand("show", "One review skill whole: its hints and its prompt");
    show->add_option("name", *skill, "The skill")->type_name(kReviewSkillValue)->required();
    add_read_format(show, show_format);
    show->callback([skill, show_format]() { show_skill(*skill, *show_format); });
}

}  // namespace apogee::commands
