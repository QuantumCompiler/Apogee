#include "cli/suite_residency.h"

#include <algorithm>
#include <cstdint>
#include <string>
#include <utility>

#include "backends/llama_runtime.h"

namespace apogee::commands {
namespace {

constexpr std::int64_t kMiB = std::int64_t{1024} * 1024;

/// The shape `/suite` takes, for a refusal to name.
constexpr std::string_view kSuiteShape = "/suite <name|off> [--force] [--warm]";

/// `bytes` in whole MiB, to the nearest -- `models::mib`'s rounding, as a
/// number to add up.
[[nodiscard]] std::int64_t whole_mib(std::int64_t bytes) {
    return (bytes + (kMiB / 2)) / kMiB;
}

/// A member's MiB as shown: its weights -- unless another member's base
/// holds them (53d) -- its cache, its adapters and its drafter (M25) each
/// rounded, so the parts on a member's line add up to its figure, and the
/// figures to the sum.
[[nodiscard]] std::optional<std::int64_t> shown_mib(const models::MemberFootprint& member) {
    if (!member.local) {
        return 0;
    }
    // `bytes()`'s rule, read part by part.
    const bool own = member.shares.empty();
    if ((own && !member.weights.has_value()) || !member.window.has_value() ||
        !member.window->cache_bytes.has_value() || !member.adapter_bytes.has_value() ||
        !member.drafter_bytes.has_value()) {
        return std::nullopt;
    }
    return (own ? whole_mib(*member.weights) : 0) + whole_mib(*member.window->cache_bytes) +
           whole_mib(*member.adapter_bytes) + whole_mib(*member.drafter_bytes);
}

/// The MiB an unknown member's drafter adds to the sum (M25), known whatever
/// the rest of the member is; nullopt when it drafts with nothing or its
/// drafter's size is unknown too.
[[nodiscard]] std::optional<std::int64_t> drafter_mib(const models::MemberFootprint& member) {
    if (!member.local || member.drafter.empty() || !member.drafter_bytes.has_value()) {
        return std::nullopt;
    }
    return whole_mib(*member.drafter_bytes);
}

/// What a member on another's base says beside its figure (53d): whose
/// weights it runs on, and what sharing them saves when that is known.
[[nodiscard]] std::string shared_note(const models::MemberFootprint& member) {
    std::string note = " (shares " + member.shares + "'s weights";
    if (member.weights.has_value()) {
        note += ", " + std::to_string(whole_mib(*member.weights)) + " MiB saved";
    }
    return note + ")";
}

/// The backend's key as the config spells it -- what its provider is
/// registered under -- for a member that names it in another case.
[[nodiscard]] std::string config_key(const harness::Config& config, const std::string& backend) {
    const auto entry = config.backends.find(backend);
    return entry != config.backends.end() ? entry->first : backend;
}

}  // namespace

models::MachineBudget machine_budget() {
    return models::MachineBudget{.bytes = backends::offload_memory_total()};
}

models::SuiteFootprint price_suite(const harness::Config& config, std::string_view suite,
                                   const MachineBudgetSource& machine) {
    models::SuiteFootprint footprint = models::suite_footprint(config, suite, {});
    if (footprint.holds_local() && machine) {
        footprint.machine = machine();
    }
    return footprint;
}

std::string admission_line(const models::SuiteFootprint& footprint, bool forced) {
    const models::Admission admission = footprint.admission();
    if (admission == models::Admission::NothingHeld) {
        return "suite " + footprint.suite + ": no member holds memory on this machine";
    }
    std::string parts;
    std::int64_t sum = 0;
    for (const models::MemberFootprint& member : footprint.members) {
        if (!member.local) {
            continue;  // a cloud member costs this machine nothing
        }
        parts += parts.empty() ? "" : " + ";
        if (const std::optional<std::int64_t> mib = shown_mib(member); mib.has_value()) {
            parts += member.backend + " " + std::to_string(*mib);
            if (!member.shares.empty()) {
                parts += shared_note(member);
            }
            sum += *mib;
        } else {
            parts += member.backend + " unknown (" + member.unknown + ")";
            // Its drafter is a load of its own, counted all the same (M25).
            if (const std::optional<std::int64_t> drafter = drafter_mib(member)) {
                parts += " + " + member.backend + "'s drafter " + std::to_string(*drafter);
                sum += *drafter;
            }
        }
    }
    const std::int64_t margin = whole_mib(models::kFitMargin);
    sum += margin;
    parts += " + " + std::to_string(margin) + " margin";

    std::string line = "suite " + footprint.suite + " needs " +
                       (footprint.has_unknown() ? "at least " : "") + std::to_string(sum) + " MiB";
    if (footprint.machine.bytes.has_value()) {
        line +=
            " of this machine's " + std::to_string(whole_mib(*footprint.machine.bytes)) + " MiB";
    }
    line += ": " + parts + " -- ";
    switch (admission) {
        case models::Admission::Fits:
            return line + "fits";
        case models::Admission::FitsAsFarAsKnown:
            return line + "fits as far as known";
        case models::Admission::BudgetUnknown:
            return line + "this machine's budget is not known here";
        case models::Admission::OverBudget:
            return line + (forced ? "over budget, run anyway (--force)" : "it does not fit");
        case models::Admission::NothingHeld:
            break;
    }
    return line;
}

std::string admission_refusal(const models::SuiteFootprint& footprint, std::string_view way_out) {
    if (footprint.admission() != models::Admission::OverBudget) {
        return {};
    }
    return admission_line(footprint) + "; " + std::string{way_out} + " runs it anyway";
}

std::vector<std::string> footprint_lines(const models::SuiteFootprint& footprint,
                                         const ResidencyProbe& resident) {
    std::vector<std::string> lines;
    lines.push_back("footprint: suite " + footprint.suite);
    for (const models::MemberFootprint& member : footprint.members) {
        std::string roles;
        for (const std::string& role : member.roles) {
            roles += (roles.empty() ? "" : ", ") + role;
        }
        std::string line = "  " + member.backend + " (" + roles + "): ";
        if (!member.local) {
            line += member.unknown.empty() ? "nothing held here" : member.unknown;
            lines.push_back(line);
            continue;
        }
        if (const std::optional<std::int64_t> mib = shown_mib(member); mib.has_value()) {
            // `shown_mib`'s parts, each said: known, since it is. Weights on
            // another member's base are that member's, said and not added.
            const models::LocalWindow window = member.window.value_or(models::LocalWindow{});
            line += std::to_string(*mib) + " MiB -- ";
            if (member.shares.empty()) {
                line += std::to_string(whole_mib(member.weights.value_or(0))) + " MiB of weights, ";
            } else if (member.weights.has_value()) {
                line += "shares " + member.shares + "'s " +
                        std::to_string(whole_mib(*member.weights)) + " MiB of weights, ";
            } else {
                line += "shares " + member.shares + "'s weights, ";
            }
            line += std::to_string(whole_mib(window.cache_bytes.value_or(0))) + " MiB of " +
                    std::string{harness::to_string(window.cache_type)} + " cache at " +
                    std::to_string(window.window) + " tokens";
            if (member.adapters > 0) {
                line += ", " + std::to_string(whole_mib(member.adapter_bytes.value_or(0))) +
                        " MiB of adapters";
            }
            if (!member.drafter.empty()) {
                line += ", " + std::to_string(whole_mib(member.drafter_bytes.value_or(0))) +
                        " MiB of its drafter's weights";
            }
        } else {
            line += "unknown -- " + member.unknown;
            if (const std::optional<std::int64_t> drafter = drafter_mib(member)) {
                line += "; its drafter's " + std::to_string(*drafter) + " MiB of weights counted";
            }
        }
        if (resident) {
            if (const std::optional<bool> loaded = resident(member.backend); loaded.has_value()) {
                line += *loaded ? " · resident" : " · not loaded";
            }
        }
        lines.push_back(line);
    }
    lines.push_back("  total: " + admission_line(footprint));
    return lines;
}

SuiteArgument parse_suite_argument(std::string_view argument) {
    SuiteArgument out;
    std::size_t at = 0;
    while (at < argument.size()) {
        const std::size_t start = argument.find_first_not_of(" \t", at);
        if (start == std::string_view::npos) {
            break;
        }
        const std::size_t end = std::min(argument.find_first_of(" \t", start), argument.size());
        const std::string_view word = argument.substr(start, end - start);
        at = end;
        if (word == "--force") {
            out.force = true;
        } else if (word == "--warm") {
            out.warm = true;
        } else if (word.starts_with("-")) {
            out.error = "unknown option '" + std::string{word} + "' -- " + std::string{kSuiteShape};
            return out;
        } else if (!out.suite.empty()) {
            out.error = "one suite at a time -- " + std::string{kSuiteShape};
            return out;
        } else {
            out.suite = std::string{word};
        }
    }
    if (out.suite.empty()) {
        out.error = "name the suite -- " + std::string{kSuiteShape};
    }
    return out;
}

std::string banner_suite(std::string_view suite, bool forced) {
    if (suite.empty()) {
        return {};
    }
    std::string said{"  ·  suite "};
    said += suite;
    if (forced) {
        said += " (over budget, --force)";
    }
    return said;
}

std::string warm_label(std::string_view suite, std::string_view backend) {
    return "warming suite " + std::string{suite} + ": loading " + std::string{backend};
}

std::vector<std::string> warm_suite(const harness::Harness& harness, const harness::Config& config,
                                    BusyLine& line) {
    std::vector<std::string> said;
    const harness::SuiteConfig* suite = harness::active_suite(config);
    if (suite == nullptr) {
        return said;
    }
    const std::string& name = config.models.default_suite;
    std::vector<std::string> backends;
    for (const harness::SuiteBackend& member : harness::suite_backends(*suite)) {
        backends.push_back(config_key(config, member.backend));
    }
    const harness::WarmResult result = harness.warm(
        backends, [&line, &name](std::string_view backend, std::size_t done, std::size_t total) {
            line.report(warm_label(name, backend), done, total);
        });
    for (const auto& [backend, why] : result.failed) {
        std::string line_said = "could not warm " + backend;
        line_said += " -- its first use will try again: ";
        line_said += why;
        said.push_back(std::move(line_said));
    }
    return said;
}

}  // namespace apogee::commands
