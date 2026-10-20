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
#include "contracts/config_edit.h"
#include "contracts/layout.h"
#include "contracts/paths.h"
#include "contracts/review_skill.h"
#include "forge/gh_transport.h"
#include "forge/glab_transport.h"
#include "harness/harness.h"
#include "harness/roles.h"
#include "machine/json_reporter.h"
#include "platform/pid_lock.h"
#include "platform/platform.h"
#include "review/sweep.h"

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

/// The posting gate for a sweep: nobody at the terminal, so the target's
/// declared grant or the run's `--allow-post` -- seeded as the run's
/// answer, never widened -- and otherwise a refusal that says the merge
/// request was reviewed into the document. `permissions.review_post: deny`
/// refuses over any grant.
[[nodiscard]] PostGate make_sweep_gate(const harness::Config& config, bool granted) {
    auto approvals = std::make_shared<SessionApprovals>();
    if (granted) {
        approvals->tools.insert(std::string{kPostTool});
    }
    agent::PermissionChecker checker = make_permission_checker(config, approvals);
    return [checker](std::string_view target, std::size_t comments, std::string& why_not) {
        const std::string detail =
            std::to_string(comments) + " inline comment(s) and a summary, as one batch";
        const agent::GateRequest request{.tool = kPostTool, .target = target, .detail = detail};
        switch (checker(request)) {
            case agent::Permission::Allow:
                return true;
            case agent::Permission::Deny:
                why_not = "permissions.review_post denies posting";
                return false;
            case agent::Permission::Ask:
                break;
        }
        why_not =
            "no grant: reviewed into this document, posted nothing (the target's post: "
            "true, or --allow-post, grants it)";
        return false;
    };
}

[[nodiscard]] std::unique_ptr<forge::ForgeClient> client_for(const harness::ReviewTargetConfig& t) {
    forge::CliTransportOptions options;
    options.executable = t.cli;
    if (t.forge == "gitlab") {
        return std::make_unique<forge::GlabTransport>(std::move(options));
    }
    return std::make_unique<forge::GhTransport>(std::move(options));
}

[[nodiscard]] nlohmann::json target_view(std::string_view name,
                                         const harness::ReviewTargetConfig& target) {
    nlohmann::json view{{"name", name},
                        {"forge", target.forge},
                        {"skills", target.skills},
                        {"post", target.post},
                        {"drafts", target.drafts}};
    if (!target.host.empty()) {
        view["host"] = target.host;
    }
    if (!target.repo.empty()) {
        view["repo"] = target.repo;
    } else {
        view["owner"] = target.owner;
    }
    if (!target.model.empty()) {
        view["model"] = target.model;
    }
    if (!target.cli.empty()) {
        view["cli"] = target.cli;
    }
    return view;
}

[[nodiscard]] harness::Config load_review_config(const RootContext& context,
                                                 std::filesystem::path& path) {
    path = harness::resolve_config_path(context.config_path);
    try {
        return harness::load_config(path);
    } catch (const harness::ConfigError& e) {
        fail(e.what());
    }
}

void list_targets(const RootContext& context, ReadFormat format) {
    std::filesystem::path path;
    const harness::Config config = load_review_config(context, path);
    if (format == ReadFormat::Json) {
        nlohmann::json data = nlohmann::json::array();
        for (const auto& [name, target] : config.review_targets) {
            data.push_back(target_view(name, target));
        }
        write_document(std::cout, nlohmann::json{{"object", "list"}, {"data", std::move(data)}});
        return;
    }
    if (config.review_targets.empty()) {
        std::cout << "no review targets -- add one: apogee review targets add <name> --repo "
                     "owner/repo\n";
        return;
    }
    for (const auto& [name, target] : config.review_targets) {
        std::cout << name << "  " << target.forge << ":"
                  << (target.repo.empty() ? target.owner + " (owner)" : target.repo)
                  << (target.host.empty() ? std::string{} : " on " + target.host)
                  << "  lenses: " << (target.skills.empty() ? "by hints" : joined(target.skills))
                  << "  posts: " << (target.post ? "yes" : "no")
                  << (target.drafts ? "  drafts: yes" : "") << "\n";
    }
}

struct TargetFlags {
    std::string name;
    std::string repo;
    std::string owner;
    std::string forge = "github";
    std::string host;
    std::vector<std::string> skills;
    bool post = false;
    bool drafts = false;
    std::string model;
    std::string cli;
    bool force = false;
};

void add_target(const RootContext& context, const TargetFlags& flags) {
    std::filesystem::path path;
    (void)load_review_config(context, path);
    harness::ReviewTargetConfig target;
    target.forge = flags.forge;
    target.host = flags.host;
    target.repo = flags.repo;
    target.owner = flags.owner;
    target.skills = flags.skills;
    target.post = flags.post;
    target.drafts = flags.drafts;
    target.model = flags.model;
    target.cli = flags.cli;
    try {
        // The editor writes it; the loader's re-read refuses a bad one, the
        // file untouched.
        harness::edit_config_file(path, [&](std::string_view content) {
            return harness::append_review_target(content, flags.name, target, flags.force);
        });
    } catch (const std::exception& e) {
        fail(e.what());
    }
    std::cout << "added review target '" << flags.name << "' to " << path.string() << "\n"
              << "It is swept by: apogee review sweep"
              << (flags.post ? "" : " (it posts only under --allow-post: post is not granted)")
              << "\n";
}

void delete_target(const RootContext& context, const std::string& name) {
    std::filesystem::path path;
    (void)load_review_config(context, path);
    try {
        harness::edit_config_file(path, [&](std::string_view content) {
            return harness::delete_review_target(content, name);
        });
    } catch (const std::exception& e) {
        fail(e.what());
    }
    std::cout << "removed review target '" << name << "' from " << path.string() << "\n";
}

struct SweepFlags {
    std::vector<std::string> targets;
    bool allow_post = false;
    bool dry_run = false;
    std::string since;
    std::string model;
    ReadFormat format = ReadFormat::Text;
};

void review_sweep(const RootContext& context, const SweepFlags& flags) {
    std::filesystem::path config_path;
    const harness::Config config = load_review_config(context, config_path);
    if (!flags.since.empty() && !forge::is_timestamp(flags.since)) {
        fail("--since takes a date or time, 2026-10-01 or 2026-10-01T09:00:00Z, not '" +
             flags.since + "'");
    }
    std::vector<review::SweepTarget> targets;
    for (const auto& [name, target] : config.review_targets) {
        targets.push_back(review::SweepTarget{.name = name, .config = target});
    }
    if (targets.empty()) {
        fail(
            "no review targets to sweep -- add one: apogee review targets add <name> --repo "
            "owner/repo");
    }
    // One sweep at a time: a second refuses, saying who holds the lock; one
    // a killed sweep left is taken over.
    platform::PidLock::Refusal refusal;
    std::optional<platform::PidLock> lock = platform::PidLock::acquire(
        harness::review_dir() / "sweep.lock", "review sweep", true, refusal);
    if (!lock.has_value()) {
        fail(refusal.holder.has_value()
                 ? "another review sweep is running (pid " + std::to_string(refusal.holder->pid) +
                       ") -- one sweep at a time"
                 : "could not take the sweep lock: " + refusal.error);
    }

    harness::Harness harness{config};
    backends::BuildOptions build_options;
    build_options.config_path = config_path;
    (void)backends::build_providers(harness, build_options);
    std::map<std::string, review::PassFn> passes;
    const auto pass_for = [&](const std::string& backend) -> const review::PassFn& {
        auto it = passes.find(backend);
        if (it == passes.end()) {
            it = passes.emplace(backend, make_pass(harness, backend)).first;
        }
        return it->second;
    };
    const auto progress = [](std::string_view line) { std::cerr << "[review] " << line << "\n"; };

    review::WatermarkStore store{harness::review_dir() / "watermarks.json"};
    review::SweepOptions options;
    options.only = flags.targets;
    if (!flags.since.empty()) {
        options.since = flags.since;
    }
    options.allow_post = flags.allow_post;
    options.dry_run = flags.dry_run;
    options.now = platform::utc_time(std::chrono::system_clock::now(), "%Y-%m-%dT%H:%M:%SZ");

    const review::ReviewOne review_one = [&](const forge::MergeRequestRef& ref,
                                             const review::SweepTarget& target, bool post) {
        const std::string model = harness::resolve_chat_backend(
            config, target.config.model.empty() ? flags.model : target.config.model);
        if (model.empty()) {
            throw std::runtime_error("no backend is configured to review with");
        }
        ReviewMrSeams seams;
        const harness::ReviewTargetConfig settings = target.config;
        seams.forge = [settings](forge::Forge) { return client_for(settings); };
        seams.pass = pass_for(model);
        const harness::Resolution utility = harness::resolve_backend(
            config,
            harness::RoleRequest{.role = harness::ModelRole::Utility, .conversation = model});
        if (harness::is_named(utility.from) && !utility.key.empty()) {
            seams.narrow = pass_for(utility.key);
        }
        seams.gate = make_sweep_gate(config, post);
        seams.progress = progress;
        seams.skills_dir = harness::review_skills_dir();
        ReviewMrOptions review;
        review.parsed = ref;
        review.skills = target.config.skills;
        review.dry_run = flags.dry_run;
        review.model = model;
        try {
            return review_merge_request(review, seams).document;
        } catch (const forge::ForgeError& e) {
            throw std::runtime_error(std::string{e.what()} +
                                     (e.remedy().empty() ? "" : " -- " + e.remedy()));
        }
    };
    nlohmann::json document;
    try {
        document = review::run_sweep(targets, store, options, client_for, review_one, progress);
    } catch (const std::runtime_error& e) {
        fail(e.what());
    }
    // Every sweep's document is kept, posted or not: an ungranted sweep's
    // findings are read here.
    const std::filesystem::path saved =
        harness::review_dir() / "sweeps" /
        (platform::utc_time(std::chrono::system_clock::now(), "%Y%m%dT%H%M%SZ") + ".json");
    std::error_code code;
    std::filesystem::create_directories(saved.parent_path(), code);
    try {
        harness::write_file_atomically(saved, document.dump(2) + "\n");
        document["document"] = saved.string();
    } catch (const std::exception& e) {
        std::cerr << "[review] the sweep's document could not be kept: " << e.what() << "\n";
    }
    lock->release();

    if (flags.format == ReadFormat::Json) {
        write_document(std::cout, document);
    } else {
        for (const nlohmann::json& target : document["targets"]) {
            if (target.contains("error")) {
                std::cout << target["name"].get<std::string>() << ": "
                          << target["error"].get<std::string>() << "\n";
            }
            for (const nlohmann::json& project : target["projects"]) {
                std::string line = project["project"].get<std::string>() + ": ";
                if (project.value("first_sweep", false)) {
                    line += "first sweep -- starting at now (--since reviews earlier ones)";
                } else {
                    line += std::to_string(project["reviewed"].size()) + " reviewed";
                    if (!project["skipped_drafts"].empty()) {
                        line += ", " + std::to_string(project["skipped_drafts"].size()) +
                                " draft(s) skipped";
                    }
                }
                std::cout << line << "\n";
                for (const nlohmann::json& run : project["reviewed"]) {
                    std::cout << "  " << run["merge_request"].get<std::string>() << ": "
                              << run["findings"].size() << " finding(s), "
                              << (run["posted"].value("done", false)
                                      ? std::string{"posted"}
                                      : "posted nothing (" +
                                            run["posted"].value("reason", std::string{}) + ")")
                              << "\n";
                }
                if (project.contains("error")) {
                    std::cout << "  stopped: " << project["error"].get<std::string>() << "\n";
                }
            }
        }
        std::cout << document["reviewed"].get<std::size_t>() << " reviewed, "
                  << document["posted"].get<std::size_t>() << " posted";
        if (document["deferred"].get<std::size_t>() > 0) {
            std::cout << ", " << document["deferred"].get<std::size_t>()
                      << " left for the next sweep (the cap is "
                      << document["cap"].get<std::size_t>() << ")";
        }
        std::cout << (document.contains("document")
                          ? " -- the document: " + document["document"].get<std::string>()
                          : std::string{})
                  << "\n";
    }
    if (document["errors"].get<std::size_t>() > 0) {
        throw CLI::RuntimeError(1);
    }
}

}  // namespace

std::string_view ReviewCommand::name() const noexcept {
    return "review";
}

std::string_view ReviewCommand::summary() const noexcept {
    return "Review merge requests with the review skills -- one, or every target swept -- and "
           "post what they find";
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

    auto sweep_flags = std::make_shared<SweepFlags>();
    auto sweep_format = std::make_shared<ReadFormat>(ReadFormat::Text);
    CLI::App* sweep = cmd->add_subcommand(
        "sweep",
        "Review what is new on every configured target since its watermark, once, and exit -- "
        "your cron or CI is the cadence");
    sweep->add_option("--target", sweep_flags->targets, "Sweep only this target (repeatable)")
        ->type_name(kReviewTargetValue);
    sweep->add_flag("--allow-post", sweep_flags->allow_post,
                    "Grant posting on every target this run sweeps (else only where a target's "
                    "post: true says)");
    sweep->add_flag("--dry-run", sweep_flags->dry_run,
                    "Review and keep the documents; post nothing");
    sweep->add_option("--since", sweep_flags->since,
                      "Where a target seen for the first time starts (default: now -- history "
                      "stays unreviewed)");
    sweep
        ->add_option("-m,--model", sweep_flags->model,
                     "The backend that reviews where a target names none")
        ->type_name(kBackendValue);
    add_read_format(sweep, sweep_format);
    sweep->callback([&context, sweep_flags, sweep_format]() {
        sweep_flags->format = *sweep_format;
        review_sweep(context, *sweep_flags);
    });

    auto targets_format = std::make_shared<ReadFormat>(ReadFormat::Text);
    CLI::App* targets = cmd->add_subcommand(
        "targets", "The repositories and owners review sweep watches (the review: section)");
    add_read_format(targets, targets_format);
    auto target_flags = std::make_shared<TargetFlags>();
    CLI::App* add = targets->add_subcommand("add", "Add a sweep target: a repository or an owner");
    add->add_option("name", target_flags->name, "The target's name")->required();
    CLI::Option* repo_option =
        add->add_option("--repo", target_flags->repo, "A repository: owner/repo, group/project");
    add->add_option("--owner", target_flags->owner,
                    "An organization, a group or a user whose repositories are swept")
        ->excludes(repo_option);
    add->add_option("--forge", target_flags->forge, "github (default) or gitlab")
        ->type_name(words_value(std::vector<std::string_view>{"github", "gitlab"}));
    add->add_option("--host", target_flags->host, "A self-hosted forge's host");
    add->add_option("--skills", target_flags->skills, "The lenses to run, comma-separated")
        ->type_name(kReviewSkillValue)
        ->delimiter(',');
    add->add_flag("--post", target_flags->post,
                  "Grant posting: a sweep posts here with nobody at the terminal");
    add->add_flag("--drafts", target_flags->drafts, "Review draft merge requests too");
    add->add_option("-m,--model", target_flags->model, "The backend that reviews this target")
        ->type_name(kBackendValue);
    add->add_option("--cli", target_flags->cli, "The gh or glab to spawn, by path")
        ->type_name(kPathValue);
    add->add_flag("--force", target_flags->force, "Replace a target of the same name");
    add->callback([&context, target_flags]() { add_target(context, *target_flags); });
    auto delete_name = std::make_shared<std::string>();
    CLI::App* remove = targets->add_subcommand("delete", "Remove a sweep target");
    remove->add_option("name", *delete_name, "The target")
        ->type_name(kReviewTargetValue)
        ->required();
    remove->callback([&context, delete_name]() { delete_target(context, *delete_name); });
    targets->callback([&context, targets_format, add, remove]() {
        if (add->parsed() || remove->parsed()) {
            return;
        }
        list_targets(context, *targets_format);
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
