#include "cli/config_suites.h"

#include <CLI/CLI.hpp>

#include <algorithm>
#include <array>
#include <filesystem>
#include <iostream>
#include <map>
#include <memory>
#include <span>
#include <utility>

#include "cli/helpers.h"
#include "contracts/config_edit.h"
#include "contracts/paths.h"
#include "operations/suites.h"
#include "symphony/definition.h"

namespace apogee::commands {
namespace {

using harness::Config;
using harness::ConfigEditError;
using harness::ConfigError;
using harness::SuiteConfig;
using harness::SuiteMember;

[[noreturn]] void fail(const std::string& message) {
    std::cerr << "apogee config: " << message << "\n";
    throw CLI::RuntimeError(1);
}

std::filesystem::path config_path_for(const RootContext& context) {
    try {
        return harness::resolve_config_path(context.config_path);
    } catch (const std::exception& e) {
        fail(e.what());
    }
}

Config load(const std::filesystem::path& path) {
    try {
        return harness::load_config(path);
    } catch (const ConfigError& e) {
        fail(e.what());
    }
}

/// Applies a pure transform, reporting either failure mode in the user's
/// terms -- the editor's refusal, or the re-parse's.
template <typename Transform>
void apply_edit(const std::filesystem::path& path, Transform&& transform) {
    try {
        harness::edit_config_file(path, std::forward<Transform>(transform));
    } catch (const ConfigEditError& e) {
        fail(e.what());
    } catch (const ConfigError& e) {
        fail(e.what());
    }
}

std::string joined(const std::vector<std::string>& names, std::string_view separator = ", ") {
    std::string out;
    for (const std::string& name : names) {
        out += out.empty() ? "" : std::string{separator};
        out += name;
    }
    return out;
}

std::string trimmed_word(std::string_view word) {
    const std::size_t first = word.find_first_not_of(" \t");
    if (first == std::string_view::npos) {
        return {};
    }
    return std::string{word.substr(first, word.find_last_not_of(" \t") - first + 1)};
}

/// A member, one line: `root`, or `helper (window 4096, toolset fs,git)`.
std::string describe_member(const SuiteMember& member) {
    std::string pins;
    if (member.context_size.has_value()) {
        pins = "window " + std::to_string(*member.context_size);
    }
    if (member.toolset.has_value()) {
        pins += std::string{pins.empty() ? "" : ", "} + "toolset " +
                (member.toolset->empty() ? std::string{"none"} : joined(*member.toolset, ","));
    }
    return pins.empty() ? member.backend : member.backend + " (" + pins + ")";
}

/// The caps a suite's consults run under, each as it holds -- `per_turn 4,
/// brief_tokens 1024, answer_tokens 512` -- marking the ones left at their
/// default.
std::string describe_caps(const SuiteConfig& suite) {
    const harness::ConsultLimits limits = harness::consult_limits(suite.consult_caps);
    const auto one = [](std::string_view name, std::int64_t value, bool set) {
        return std::string{name} + " " + std::to_string(value) + (set ? "" : " (default)");
    };
    return one("per_turn", limits.per_turn, suite.consult_caps.per_turn.has_value()) + ", " +
           one("brief_tokens", limits.brief_tokens, suite.consult_caps.brief_tokens.has_value()) +
           ", " +
           one("answer_tokens", limits.answer_tokens, suite.consult_caps.answer_tokens.has_value());
}

/// The member flags every verb shares: one backend per role, and the knobs --
/// and the suite's consultable members and their caps (27f).
struct MemberFlags {
    /// Role name -> the backend its flag gave; empty when not given.
    std::map<std::string, std::string, std::less<>> backends;
    std::vector<std::string> context_sizes;
    std::vector<std::string> toolsets;
    /// `--consultable`, when given -- "" for none.
    std::optional<std::string> consultable;
    std::vector<std::string> consult_caps;
    /// `--verifier`, when given -- "" for the default (27g).
    std::optional<std::string> verifier;
    /// `--validate SEAM=VALUE`, each; `off` clears the block.
    std::vector<std::string> validate;
    /// `--orchestrate on|off`, when given (27t).
    std::optional<std::string> orchestrate;
};

/// What `--orchestrate` takes (27t).
constexpr std::array<std::string_view, 2> kOrchestrateWords{"on", "off"};

/// `tool_args=`, ...: what `--validate` offers, and `off`.
const std::vector<std::string>& seam_prefixes() {
    static const std::vector<std::string> prefixes = [] {
        std::vector<std::string> out;
        for (const std::string_view seam : harness::validate_seam_names()) {
            out.push_back(std::string{seam} + "=");
        }
        out.emplace_back("off");
        return out;
    }();
    return prefixes;
}

/// `per_turn=`, ...: what `--consult-cap` offers.
const std::vector<std::string>& cap_prefixes() {
    static const std::vector<std::string> prefixes = [] {
        std::vector<std::string> out;
        for (const std::string_view cap : harness::consult_cap_names()) {
            out.push_back(std::string{cap} + "=");
        }
        return out;
    }();
    return prefixes;
}

/// `chat=`, `embedding=`, ...: what `--context-size` and `--toolset` offer,
/// the role to type the value after.
const std::vector<std::string>& role_prefixes() {
    static const std::vector<std::string> prefixes = [] {
        std::vector<std::string> out;
        for (const std::string_view role : harness::suite_role_names()) {
            out.push_back(std::string{role} + "=");
        }
        return out;
    }();
    return prefixes;
}

void bind_member_flags(CLI::App& cmd, MemberFlags& flags) {
    for (const std::string_view role : harness::suite_role_names()) {
        std::string& slot = flags.backends[std::string{role}];
        cmd.add_option("--" + std::string{role}, slot,
                       "The backend that answers for the " + std::string{role} + " role")
            ->type_name(kBackendValue);
    }
    cmd.add_option("--context-size", flags.context_sizes,
                   "Pin a member's window: ROLE=TOKENS, e.g. utility=4096 (repeatable)")
        ->type_name(words_value(role_prefixes()))
        ->expected(1)
        ->allow_extra_args(false);
    cmd.add_option("--toolset", flags.toolsets,
                   "Pin the tools a member is offered: ROLE=fs,git,... -- fs, shell, git, notes, "
                   "rag, graph, web, mcp; ROLE= for none (repeatable)")
        ->type_name(words_value(role_prefixes()))
        ->expected(1)
        ->allow_extra_args(false);
    cmd.add_option_function<std::string>(
           "--consultable", [&flags](const std::string& value) { flags.consultable = value; },
           "The members the chat model may consult through the consult tool: ROLE,ROLE -- "
           "local, unmetered members only; \"\" for none")
        ->type_name(words_value(harness::consultable_role_names()))
        ->expected(1);
    cmd.add_option("--consult-cap", flags.consult_caps,
                   "Bound the consults: per_turn=N, brief_tokens=N, answer_tokens=N; NAME= for "
                   "the default (repeatable)")
        ->type_name(words_value(cap_prefixes()))
        ->expected(1)
        ->allow_extra_args(false)
        ->multi_option_policy(CLI::MultiOptionPolicy::TakeAll);
    cmd.add_option_function<std::string>(
           "--verifier", [&flags](const std::string& value) { flags.verifier = value; },
           "The member that checks the others' work when a validate seam is on: a ROLE -- "
           "local, unmetered members only; \"\" for the default (utility)")
        ->type_name(words_value(harness::consultable_role_names()))
        ->expected(1);
    cmd.add_option("--validate", flags.validate,
                   "Have the verifier check a seam: tool_args=on|off, extraction=on|off, "
                   "answers=request|always; SEAM= for the default; off for no validation at all "
                   "(repeatable)")
        ->type_name(words_value(seam_prefixes()))
        ->expected(1)
        ->allow_extra_args(false)
        ->multi_option_policy(CLI::MultiOptionPolicy::TakeAll);
    cmd.add_option_function<std::string>(
           "--orchestrate", [&flags](const std::string& value) { flags.orchestrate = value; },
           "Let an execute session's chat model play the suite's symphonies on its own "
           "initiative, each offered as a tool: on or off -- symphonies reaching local, "
           "unmetered members only")
        ->type_name(words_value(kOrchestrateWords))
        ->expected(1);
}

/// `--validate SEAM=on|off`'s value: on, off, or empty for the default.
std::optional<bool> switch_value(const std::string& text, std::string_view seam,
                                 std::string_view value) {
    if (value == "on") {
        return true;
    }
    if (value == "off") {
        return false;
    }
    if (!value.empty()) {
        fail("--validate " + text + ": " + std::string{seam} + " is on or off");
    }
    return std::nullopt;
}

/// `--validate answers=request|always`'s value, or empty for the default.
std::optional<std::string> answers_value(const std::string& text, std::string value) {
    if (value.empty()) {
        return std::nullopt;
    }
    const std::span<const std::string_view> whens = harness::answer_check_names();
    if (std::ranges::find(whens, value) == whens.end()) {
        fail("--validate " + text + ": answers is request or always");
    }
    return value;
}

/// The validate flags applied onto `suite` (27g), in the order given: `off`
/// clears the whole block, `SEAM=VALUE` sets a seam, `SEAM=` resets it.
void apply_validate(SuiteConfig& suite, const MemberFlags& flags) {
    for (const std::string& text : flags.validate) {
        if (trimmed_word(text) == "off") {
            suite.validate = {};
            continue;
        }
        const std::size_t equals = text.find('=');
        const std::string seam =
            trimmed_word(equals == std::string::npos ? std::string_view{text}
                                                     : std::string_view{text}.substr(0, equals));
        const std::span<const std::string_view> seams = harness::validate_seam_names();
        if (equals == std::string::npos || std::ranges::find(seams, seam) == seams.end()) {
            std::vector<std::string> accepted;
            for (const std::string_view name : seams) {
                accepted.emplace_back(name);
            }
            fail("--validate " + text + ": not SEAM=VALUE with SEAM one of " + joined(accepted) +
                 ", nor off");
        }
        std::string value = trimmed_word(std::string_view{text}.substr(equals + 1));
        if (seam == "answers") {
            suite.validate.answers = answers_value(text, std::move(value));
        } else if (seam == "tool_args") {
            suite.validate.tool_args = switch_value(text, seam, value);
        } else {
            suite.validate.extraction = switch_value(text, seam, value);
        }
    }
    if (flags.verifier.has_value()) {
        const std::string role = trimmed_word(*flags.verifier);
        suite.validate.verifier = role.empty() ? std::nullopt : std::optional<std::string>{role};
    }
}

/// The orchestrate flag applied onto `suite` (27t): `on` or `off`.
void apply_orchestrate(SuiteConfig& suite, const MemberFlags& flags) {
    if (!flags.orchestrate.has_value()) {
        return;
    }
    const std::string value = trimmed_word(*flags.orchestrate);
    if (std::ranges::find(kOrchestrateWords, value) == kOrchestrateWords.end()) {
        fail("--orchestrate " + *flags.orchestrate + ": on or off");
    }
    suite.orchestrate = value == kOrchestrateWords.front();
}

/// The symphonies a suite in the config at `path` would offer the model
/// (27t): every source, the spec files beside that config's data.
symphony::Catalog symphonies_for(const Config& config, const std::filesystem::path& path) {
    return symphony::catalog(config, symphony::directory_for(path));
}

/// A suite's validation as it holds -- `verifier utility (default),
/// tool_args on, extraction off (default), answers request (default)` (27g).
std::string describe_validate(const SuiteConfig& suite) {
    const harness::ValidatePolicy policy = harness::validate_policy(suite.validate);
    const harness::ValidateConfig& set = suite.validate;
    const auto mark = [](bool given) { return given ? std::string{} : std::string{" (default)"}; };
    return "verifier " + policy.verifier + mark(set.verifier.has_value()) + ", tool_args " +
           (policy.tool_args ? "on" : "off") + mark(set.tool_args.has_value()) + ", extraction " +
           (policy.extraction ? "on" : "off") + mark(set.extraction.has_value()) + ", answers " +
           (policy.answers_always ? "always" : "request") + mark(set.answers.has_value());
}

/// `--consultable`'s roles, as given: comma-separated, blanks dropped.
std::vector<std::string> consultable_roles(std::string_view text) {
    std::vector<std::string> out;
    std::size_t start = 0;
    while (start <= text.size()) {
        const std::size_t comma = text.find(',', start);
        const std::string word = trimmed_word(text.substr(
            start, comma == std::string_view::npos ? std::string_view::npos : comma - start));
        if (!word.empty()) {
            out.push_back(word);
        }
        if (comma == std::string_view::npos) {
            break;
        }
        start = comma + 1;
    }
    return out;
}

/// The consult flags applied onto `suite` (27f).
void apply_consult(SuiteConfig& suite, const MemberFlags& flags) {
    if (flags.consultable.has_value()) {
        suite.consultable = consultable_roles(*flags.consultable);
    }
    for (const std::string& text : flags.consult_caps) {
        const std::size_t equals = text.find('=');
        const std::string name = equals == std::string::npos ? text : text.substr(0, equals);
        const std::span<const std::string_view> names = harness::consult_cap_names();
        if (equals == std::string::npos || std::ranges::find(names, name) == names.end()) {
            std::vector<std::string> accepted;
            for (const std::string_view cap : names) {
                accepted.emplace_back(cap);
            }
            fail("--consult-cap " + text + ": not NAME=N with NAME one of " + joined(accepted));
        }
        const std::string value = trimmed_word(text.substr(equals + 1));
        std::optional<std::int64_t> parsed;
        if (!value.empty()) {
            std::int64_t number = 0;
            try {
                std::size_t used = 0;
                number = std::stoll(value, &used);
                if (used != value.size()) {
                    number = 0;
                }
            } catch (const std::exception&) {
                number = 0;
            }
            if (number < 1) {
                fail("--consult-cap " + text + ": '" + value + "' is not a positive whole number");
            }
            parsed = number;
        }
        if (name == "per_turn") {
            suite.consult_caps.per_turn = parsed;
        } else if (name == "brief_tokens") {
            suite.consult_caps.brief_tokens = parsed;
        } else {
            suite.consult_caps.answer_tokens = parsed;
        }
    }
}

/// The knobs the flags pin, applied onto `suite`'s members.
void apply_knobs(SuiteConfig& suite, const MemberFlags& flags) {
    for (const std::string& text : flags.context_sizes) {
        RoleArgument argument;
        if (const std::string refused = parse_role_argument(text, argument); !refused.empty()) {
            fail("--context-size: " + refused);
        }
        const auto member = suite.members.find(argument.role);
        if (member == suite.members.end()) {
            fail("--context-size " + text + ": the suite has no " + argument.role +
                 " member -- name its backend with --" + argument.role);
        }
        std::int64_t tokens = 0;
        try {
            std::size_t used = 0;
            tokens = std::stoll(argument.value, &used);
            if (used != argument.value.size()) {
                tokens = 0;
            }
        } catch (const std::exception&) {
            tokens = 0;
        }
        if (tokens < 1) {
            fail("--context-size " + text + ": '" + argument.value +
                 "' is not a positive number of tokens");
        }
        member->second.context_size = tokens;
    }
    for (const std::string& text : flags.toolsets) {
        RoleArgument argument;
        if (const std::string refused = parse_role_argument(text, argument); !refused.empty()) {
            fail("--toolset: " + refused);
        }
        const auto member = suite.members.find(argument.role);
        if (member == suite.members.end()) {
            fail("--toolset " + text + ": the suite has no " + argument.role +
                 " member -- name its backend with --" + argument.role);
        }
        std::vector<std::string> toolset;
        if (const std::string refused = parse_toolset(argument.value, toolset); !refused.empty()) {
            fail("--toolset " + text + ": " + refused);
        }
        member->second.toolset = std::move(toolset);
    }
}

void bind_add_suite(CLI::App& parent, const RootContext& context) {
    struct Flags {
        std::string name;
        std::string description;
        MemberFlags members;
        bool force = false;
    };

    auto flags = std::make_shared<Flags>();
    CLI::App* cmd = parent.add_subcommand(
        "add-suite", "Add a suite: a named bundle of backends, one per role it speaks for");
    cmd->add_option("name", flags->name, "Name for the suite")->required();
    bind_member_flags(*cmd, flags->members);
    cmd->add_option("--description", flags->description, "What the suite is for, for listings");
    cmd->add_flag("-f,--force", flags->force, "Replace an existing suite with this name");
    cmd->callback([&context, flags]() {
        const std::filesystem::path path = config_path_for(context);
        const Config config = load(path);
        SuiteConfig suite;
        suite.description = flags->description;
        for (const auto& [role, backend] : flags->members.backends) {
            if (!backend.empty()) {
                suite.members[role] = SuiteMember{.backend = backend};
            }
        }
        apply_knobs(suite, flags->members);
        apply_consult(suite, flags->members);
        apply_validate(suite, flags->members);
        apply_orchestrate(suite, flags->members);
        const symphony::Catalog symphonies = symphonies_for(config, path);
        if (const std::string refused = validate_suite(config, flags->name, suite,
                                                       provider_metered_probe(path), &symphonies);
            !refused.empty()) {
            fail("add-suite: " + refused);
        }
        apply_edit(path, [flags, &suite](std::string_view content) {
            return harness::append_suite(content, flags->name, suite, flags->force);
        });
        std::cout << "added suite '" << flags->name << "' to " << path.string()
                  << " -- use it with: apogee chat --suite " << flags->name << "\n";
    });
}

void bind_set_suite(CLI::App& parent, const RootContext& context) {
    struct Flags {
        std::string name;
        MemberFlags members;
        std::vector<std::string> unpin;
        std::vector<std::string> remove;
    };

    auto flags = std::make_shared<Flags>();
    CLI::App* cmd = parent.add_subcommand(
        "set-suite", "Change a suite's members: a role's backend, its pins, or the member itself");
    cmd->add_option("name", flags->name, "The suite")->type_name(kModelSuiteValue)->required();
    bind_member_flags(*cmd, flags->members);
    cmd->add_option("--unpin", flags->unpin, "Drop a member's window and toolset pins (repeatable)")
        ->type_name(words_value(harness::suite_role_names()))
        ->expected(1)
        ->allow_extra_args(false);
    cmd->add_option("--remove", flags->remove,
                    "Remove a member: the role falls through to the global pointers (repeatable)")
        ->type_name(words_value(harness::suite_role_names()))
        ->expected(1)
        ->allow_extra_args(false);
    cmd->callback([&context, flags]() {
        const std::filesystem::path path = config_path_for(context);
        const Config config = load(path);
        const auto found = config.suites.find(flags->name);
        if (found == config.suites.end()) {
            const std::vector<std::string> known = config.suite_names();
            fail("no suite named '" + flags->name + "'" +
                 (known.empty() ? " (none is configured -- 'apogee config add-suite')"
                                : " (configured: " + joined(known) + ")"));
        }
        const SuiteConfig before = found->second;
        SuiteConfig after = before;
        const auto is_role = [](const std::string& role) {
            const std::span<const std::string_view> roles = harness::suite_role_names();
            return std::ranges::find(roles, role) != roles.end();
        };
        for (const auto& [role, backend] : flags->members.backends) {
            if (!backend.empty()) {
                after.members[role].backend = backend;  // its pins kept
            }
        }
        for (const std::string& role : flags->remove) {
            if (!is_role(role) || after.members.erase(role) == 0) {
                fail("--remove " + role + ": the suite has no " + role + " member");
            }
        }
        for (const std::string& role : flags->unpin) {
            const auto member = after.members.find(role);
            if (!is_role(role) || member == after.members.end()) {
                fail("--unpin " + role + ": the suite has no " + role + " member");
            }
            member->second.context_size.reset();
            member->second.toolset.reset();
        }
        apply_knobs(after, flags->members);
        apply_consult(after, flags->members);
        apply_validate(after, flags->members);
        apply_orchestrate(after, flags->members);
        const symphony::Catalog symphonies = symphonies_for(config, path);
        if (const std::string refused = validate_suite(config, flags->name, after,
                                                       provider_metered_probe(path), &symphonies);
            !refused.empty()) {
            fail("set-suite: " + refused);
        }
        if (after == before) {
            fail(
                "set-suite: nothing to change -- name a member with --<role>, or pass "
                "--context-size, --toolset, --unpin, --remove, --consultable, --consult-cap, "
                "--verifier, --validate or --orchestrate");
        }
        // One member at a time, each in place: every other line of the
        // entry, its comments included, stays as it was.
        apply_edit(path, [flags, &before, &after](std::string_view content) {
            std::string edited{content};
            for (const std::string_view role : harness::suite_role_names()) {
                const auto was = before.members.find(role);
                const auto now = after.members.find(role);
                const std::optional<SuiteMember> old_member =
                    was == before.members.end() ? std::nullopt : std::optional{was->second};
                const std::optional<SuiteMember> new_member =
                    now == after.members.end() ? std::nullopt : std::optional{now->second};
                if (old_member != new_member) {
                    edited = harness::set_suite_member(edited, flags->name, role, new_member);
                }
            }
            if (after.consultable != before.consultable) {
                edited = harness::set_suite_consultable(edited, flags->name, after.consultable);
            }
            if (after.consult_caps != before.consult_caps) {
                edited = harness::set_suite_consult_caps(edited, flags->name, after.consult_caps);
            }
            if (after.validate != before.validate) {
                edited = harness::set_suite_validate(edited, flags->name, after.validate);
            }
            if (after.orchestrate != before.orchestrate) {
                edited = harness::set_suite_orchestrate(edited, flags->name, after.orchestrate);
            }
            return edited;
        });
        std::cout << "updated suite '" << flags->name << "' in " << path.string() << "\n";
    });
}

void bind_delete_suite(CLI::App& parent, const RootContext& context) {
    auto name = std::make_shared<std::string>();
    CLI::App* cmd = parent.add_subcommand("delete-suite", "Remove a suite's entry");
    cmd->add_option("name", *name, "The suite")->type_name(kModelSuiteValue)->required();
    cmd->callback([&context, name]() {
        const std::filesystem::path path = config_path_for(context);
        if (const std::string refused = validate_suite_delete(load(path), *name);
            !refused.empty()) {
            fail("delete-suite: " + refused);
        }
        apply_edit(path, [name](std::string_view content) {
            return harness::delete_suite(content, *name);
        });
        std::cout << "removed suite '" << *name << "' from " << path.string() << "\n";
    });
}

void bind_set_default_suite(CLI::App& parent, const RootContext& context) {
    auto name = std::make_shared<std::string>();
    CLI::App* cmd = parent.add_subcommand(
        "set-default-suite",
        "Set the suite every command resolves its roles under, or off for none");
    cmd->add_option("name", *name, "The suite, or off")->type_name(kModelSuiteValue)->required();
    cmd->callback([&context, name]() {
        const std::filesystem::path path = config_path_for(context);
        std::string value;
        if (*name != harness::kSuiteOff) {
            const Config config = load(path);
            const auto found = config.suites.find(*name);
            if (found == config.suites.end()) {
                const std::vector<std::string> known = config.suite_names();
                fail("no suite named '" + *name + "'" +
                     (known.empty() ? " (none is configured -- 'apogee config add-suite')"
                                    : " (configured: " + joined(known) + ")"));
            }
            value = found->first;
        }
        apply_edit(path, [&value](std::string_view content) {
            return harness::set_default_suite(content, value);
        });
        std::cout << "models.default_suite = " << (value.empty() ? "(none)" : value) << "\n";
    });
}

}  // namespace

std::string parse_role_argument(std::string_view text, RoleArgument& out) {
    const std::size_t equals = text.find('=');
    if (equals == std::string_view::npos) {
        return "'" + std::string{text} + "' is not ROLE=VALUE";
    }
    const std::string_view role = text.substr(0, equals);
    const std::span<const std::string_view> roles = harness::suite_role_names();
    if (std::ranges::find(roles, role) == roles.end()) {
        std::vector<std::string> accepted;
        for (const std::string_view name : roles) {
            accepted.emplace_back(name);
        }
        return "'" + std::string{role} + "' is not a role (accepted: " + joined(accepted) + ")";
    }
    out.role = std::string{role};
    out.value = std::string{text.substr(equals + 1)};
    return {};
}

std::string parse_toolset(std::string_view text, std::vector<std::string>& out) {
    out.clear();
    const std::span<const std::string_view> names = harness::suite_toolset_names();
    std::size_t start = 0;
    while (start <= text.size()) {
        const std::size_t comma = text.find(',', start);
        std::string_view word = text.substr(
            start, comma == std::string_view::npos ? std::string_view::npos : comma - start);
        const std::size_t first = word.find_first_not_of(" \t");
        word = first == std::string_view::npos
                   ? std::string_view{}
                   : word.substr(first, word.find_last_not_of(" \t") - first + 1);
        if (!word.empty()) {
            if (std::ranges::find(names, word) == names.end()) {
                std::vector<std::string> accepted;
                for (const std::string_view name : names) {
                    accepted.emplace_back(name);
                }
                return "'" + std::string{word} +
                       "' is not a toolset (accepted: " + joined(accepted) + ")";
            }
            if (std::ranges::find(out, word) == out.end()) {
                out.emplace_back(word);
            }
        }
        if (comma == std::string_view::npos) {
            break;
        }
        start = comma + 1;
    }
    return {};
}

std::optional<std::string> suite_lookup(const Config& config, std::string_view key) {
    if (key == "models.default_suite") {
        return config.models.default_suite;
    }
    if (key == "suites") {
        return joined(config.suite_names(), "\n");
    }
    constexpr std::string_view prefix = "suites.";
    if (!key.starts_with(prefix)) {
        return std::nullopt;
    }
    const std::string_view rest = key.substr(prefix.size());
    const std::size_t dot = rest.find('.');
    const SuiteConfig* suite = config.find_suite(rest.substr(0, dot));
    if (suite == nullptr) {
        return std::nullopt;
    }
    if (dot == std::string_view::npos) {
        // The entry itself: its members, one per line, in role order -- and
        // whom the chat model may consult (27f).
        std::vector<std::string> lines;
        for (const std::string_view role : harness::suite_role_names()) {
            if (const auto it = suite->members.find(role); it != suite->members.end()) {
                lines.push_back(std::string{role} + ": " + describe_member(it->second));
            }
        }
        if (!suite->consultable.empty()) {
            lines.push_back("consultable: " + joined(suite->consultable));
        }
        if (suite->consult_caps.any()) {
            lines.push_back("consult_caps: " + describe_caps(*suite));
        }
        if (suite->validate.any()) {
            lines.push_back("validate: " + describe_validate(*suite));
        }
        if (suite->orchestrate) {
            lines.emplace_back("orchestrate: on");
        }
        return joined(lines, "\n");
    }
    const std::string_view field = rest.substr(dot + 1);
    if (field == "description") {
        return suite->description;
    }
    if (field == "consultable") {
        return joined(suite->consultable);
    }
    if (field == "consult_caps") {
        return describe_caps(*suite);
    }
    if (field == "validate") {
        return describe_validate(*suite);
    }
    if (field == "orchestrate") {
        return std::string{suite->orchestrate ? "on" : "off"};
    }
    const std::span<const std::string_view> roles = harness::suite_role_names();
    if (std::ranges::find(roles, field) == roles.end()) {
        return std::nullopt;
    }
    const auto it = suite->members.find(field);
    return it == suite->members.end() ? std::string{} : describe_member(it->second);
}

std::string active_suite_summary(const Config& config) {
    const SuiteConfig* suite = harness::active_suite(config);
    if (suite == nullptr) {
        return "none -- /suite <name> runs the chat under one";
    }
    std::string members;
    for (const std::string_view role : harness::suite_role_names()) {
        if (const auto it = suite->members.find(role); it != suite->members.end()) {
            members += members.empty() ? "" : " · ";
            members += std::string{role} + " " + it->second.backend;
        }
    }
    return config.models.default_suite + (members.empty() ? "" : " -- " + members);
}

void append_suite_keys(const Config& config, std::vector<std::string>& keys) {
    keys.emplace_back("models.default_suite");
    keys.emplace_back("suites");
    for (const std::string& name : config.suite_names()) {
        const std::string prefix = "suites." + name;
        keys.push_back(prefix);
        keys.push_back(prefix + ".description");
        keys.push_back(prefix + ".consultable");
        keys.push_back(prefix + ".consult_caps");
        keys.push_back(prefix + ".validate");
        keys.push_back(prefix + ".orchestrate");
        for (const std::string_view role : harness::suite_role_names()) {
            keys.push_back(prefix + "." + std::string{role});
        }
    }
}

void bind_suite_verbs(CLI::App& parent, const RootContext& context) {
    bind_add_suite(parent, context);
    bind_set_suite(parent, context);
    bind_delete_suite(parent, context);
    bind_set_default_suite(parent, context);
}

}  // namespace apogee::commands
