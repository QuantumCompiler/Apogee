#include "cli/review_cmd.h"

#include <CLI/CLI.hpp>

#include <algorithm>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

#include "agentloop/reporter.h"
#include "agentloop/structured.h"
#include "backends/factory.h"
#include "cli/helpers.h"
#include "cli/permissions.h"
#include "cli/review_mr.h"
#include "contracts/config.h"
#include "contracts/layout.h"
#include "contracts/paths.h"
#include "contracts/review_skill.h"
#include "forge/gh_transport.h"
#include "forge/glab_transport.h"
#include "harness/harness.h"
#include "harness/roles.h"
#include "machine/json_reporter.h"
#include "platform/platform.h"

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

/// The gated name posting goes by: `permissions.review_post` in the config,
/// `--yes` for a run, the session's mode over both.
constexpr std::string_view kPostTool = "review_post";

/// The most a review pass may answer.
constexpr std::int64_t kReviewAnswerTokens = 2048;

struct MrFlags {
    std::string ref;
    std::vector<std::string> skills;
    bool dry_run = false;
    bool yes = false;
    std::string model;
    std::string permission_mode;
    ReadFormat format = ReadFormat::Text;
};

/// A pass on `backend`: the system prompt and the user message as a
/// throwaway history (a side request, so a local model's session cache
/// stays), the model's thinking off -- left on, a small local model spent
/// its whole answer thinking -- and the answer held to the schema by the
/// structured-output path, retried once, validated always.
[[nodiscard]] review::PassFn make_pass(const harness::Harness& harness, std::string backend) {
    return [&harness, backend = std::move(backend)](const review::PassRequest& request) {
        review::PassAnswer answer;
        std::vector<harness::ChatMessage> history{harness::ChatMessage::system(request.system),
                                                  harness::ChatMessage::user(request.user)};
        agentloop::Options options;
        options.model = backend;
        options.max_tokens = kReviewAnswerTokens;
        options.stream_answer = false;
        options.side_request = true;
        options.thinking.mode = harness::ThinkingMode::Off;
        agentloop::NullReporter reporter;
        try {
            const agentloop::StructuredResult result = agentloop::run_structured(
                harness, history, options, reporter, std::string_view{request.schema});
            answer.json = result.json;
            answer.conforms = result.conforms;
            for (const std::string& error : result.errors) {
                answer.error += (answer.error.empty() ? "" : "; ") + error;
            }
        } catch (const std::exception& e) {
            answer.error = e.what();
        }
        return answer;
    };
}

/// The gate posting goes through: the config's `permissions.review_post`,
/// the run's `--yes`, the mode -- then, on a terminal, the question, its
/// answer taken through the one path every prompt answers by; with nobody
/// to ask, a refusal that names `--yes`.
[[nodiscard]] PostGate make_gate(const harness::Config& config,
                                 const std::filesystem::path& config_path, const MrFlags& flags) {
    auto approvals = std::make_shared<SessionApprovals>();
    if (flags.yes) {
        approvals->tools.insert(std::string{kPostTool});
    }
    if (!flags.permission_mode.empty()) {
        const std::optional<PermissionMode> mode =
            permission_mode_from_string(flags.permission_mode);
        if (mode.has_value()) {
            approvals->mode = *mode;
            approvals->bypass_allowed = *mode == PermissionMode::Bypass;
        }
    }
    agent::PermissionChecker checker = make_permission_checker(config, approvals);
    return [checker, approvals, config_path](std::string_view target, std::size_t comments,
                                             std::string& why_not) {
        const std::string detail =
            std::to_string(comments) + " inline comment(s) and a summary, as one batch";
        const agent::GateRequest request{.tool = kPostTool, .target = target, .detail = detail};
        switch (checker(request)) {
            case agent::Permission::Allow:
                return true;
            case agent::Permission::Deny:
                why_not = approvals->mode == PermissionMode::Plan
                              ? "plan mode is on: nothing is posted until it is left"
                          : approvals->mode == PermissionMode::DontAsk
                              ? "this run refuses whatever it would ask (dont-ask mode)"
                              : "permissions.review_post denies posting";
                return false;
            case agent::Permission::Ask:
                break;
        }
        if (!platform::is_terminal(platform::StandardStream::In) ||
            !platform::is_terminal(platform::StandardStream::Err)) {
            why_not = "nobody to ask -- run with --yes to post, or --dry-run to review only";
            return false;
        }
        std::cerr << "post " << detail << " to " << target
                  << "? [y]es / [n]o / [a]lways / [s]ession: " << std::flush;
        std::string answer;
        std::getline(std::cin, answer);
        std::string note;
        const bool allowed = answer_permission(answer, request, config_path, approvals, note);
        if (!note.empty()) {
            std::cerr << note << "\n";
        }
        if (!allowed) {
            why_not = "posting was declined";
        }
        return allowed;
    };
}

void review_mr(const RootContext& context, const MrFlags& flags) {
    const std::filesystem::path config_path = harness::resolve_config_path(context.config_path);
    harness::Config config;
    try {
        config = harness::load_config(config_path);
    } catch (const harness::ConfigError& e) {
        fail(e.what());
    }
    if (!flags.permission_mode.empty() &&
        !permission_mode_from_string(flags.permission_mode).has_value()) {
        fail("--permission-mode takes manual, accept-edits, plan, dont-ask or bypass, not '" +
             flags.permission_mode + "'");
    }
    const std::string model = harness::resolve_chat_backend(config, flags.model);
    if (model.empty()) {
        fail("no backend is configured to review with (add one with 'apogee config add-backend')");
    }
    harness::Harness harness{config};
    backends::BuildOptions build_options;
    build_options.config_path = config_path;
    const backends::BuildResult built = backends::build_providers(harness, build_options);
    for (const backends::BackendStatus& status : built.statuses) {
        if (status.name == model && !status.constructed) {
            fail("backend '" + model + "' is configured but unavailable -- " + status.reason);
        }
    }

    ReviewMrSeams seams;
    seams.forge = [](forge::Forge which) -> std::unique_ptr<forge::ForgeClient> {
        if (which == forge::Forge::GitHub) {
            return std::make_unique<forge::GhTransport>();
        }
        return std::make_unique<forge::GlabTransport>();
    };
    seams.pass = make_pass(harness, model);
    // The plan narrows only on a utility member the config names.
    const harness::Resolution utility = harness::resolve_backend(
        config, harness::RoleRequest{.role = harness::ModelRole::Utility, .conversation = model});
    if (harness::is_named(utility.from) && !utility.key.empty()) {
        seams.narrow = make_pass(harness, utility.key);
    }
    seams.gate = make_gate(config, config_path, flags);
    seams.progress = [](std::string_view line) { std::cerr << "[review] " << line << "\n"; };
    seams.skills_dir = harness::review_skills_dir();
    if (const harness::BackendConfig* entry = config.find_backend(model);
        entry != nullptr && entry->context_size.value_or(0) > 0) {
        seams.chunk_tokens = std::clamp<std::size_t>(
            static_cast<std::size_t>(*entry->context_size) / 3, 1500, 12000);
    }

    ReviewMrOptions options;
    options.ref = flags.ref;
    options.skills = flags.skills;
    options.dry_run = flags.dry_run;
    options.model = model;
    ReviewMrResult result;
    try {
        result = review_merge_request(options, seams);
    } catch (const forge::ForgeError& e) {
        fail(std::string{e.what()} + (e.remedy().empty() ? "" : " -- " + e.remedy()));
    } catch (const std::runtime_error& e) {
        fail(e.what());
    }
    if (flags.format == ReadFormat::Json) {
        write_document(std::cout, result.document);
        return;
    }
    for (const std::string& line : result.lines) {
        std::cout << line << "\n";
    }
}

}  // namespace

std::string_view ReviewCommand::name() const noexcept {
    return "review";
}

std::string_view ReviewCommand::summary() const noexcept {
    return "Review a merge request with the review skills, and post what it finds";
}

void ReviewCommand::bind(CLI::App& root, const RootContext& context) {
    CLI::App* cmd = root.add_subcommand(std::string{name()}, std::string{summary()});
    cmd->require_subcommand(1);

    auto mr_flags = std::make_shared<MrFlags>();
    auto mr_format = std::make_shared<ReadFormat>(ReadFormat::Text);
    CLI::App* mr = cmd->add_subcommand(
        "mr",
        "Review one merge request -- a URL, owner/repo#12 or group/project!4 -- and post its "
        "findings as one batch of inline comments");
    mr->add_option("ref", mr_flags->ref, "The merge request")->required();
    mr->add_flag("--dry-run", mr_flags->dry_run, "Review and print; post nothing");
    mr->add_option("--skills", mr_flags->skills,
                   "Run exactly these skills, comma-separated, whatever their hints say")
        ->type_name(kReviewSkillValue)
        ->delimiter(',');
    mr->add_option("-m,--model", mr_flags->model,
                   "The backend that reviews (default: the chat role)")
        ->type_name(kBackendValue);
    mr->add_flag("--yes", mr_flags->yes, "Post without asking (review_post allowed for this run)");
    mr->add_option("--permission-mode", mr_flags->permission_mode,
                   "How much runs without asking: manual, accept-edits, plan, dont-ask or bypass")
        ->type_name(words_value(permission_mode_words()));
    add_read_format(mr, mr_format);
    mr->callback([&context, mr_flags, mr_format]() {
        mr_flags->format = *mr_format;
        review_mr(context, *mr_flags);
    });

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
