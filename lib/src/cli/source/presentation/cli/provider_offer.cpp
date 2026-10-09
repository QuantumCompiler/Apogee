#include "cli/provider_offer.h"

#include <algorithm>
#include <cctype>
#include <istream>
#include <ostream>
#include <string>

#include "backends/provider_table.h"
#include "contracts/config_edit.h"

namespace apogee::commands {

namespace {

[[nodiscard]] std::string lowered(std::string_view text) {
    std::string out{text};
    std::ranges::transform(out, out.begin(),
                           [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return out;
}

/// The configured backend named `name`, compared as the editor compares
/// names -- case aside -- or null.
[[nodiscard]] const std::pair<const std::string, harness::BackendConfig>* named(
    const harness::Config& config, std::string_view name) {
    const std::string wanted = lowered(name);
    for (const auto& entry : config.backends) {
        if (lowered(entry.first) == wanted) {
            return &entry;
        }
    }
    return nullptr;
}

/// "claude", "claude and codex", "claude, codex and gemini".
[[nodiscard]] std::string joined(const std::vector<std::string>& names) {
    std::string out;
    for (std::size_t i = 0; i < names.size(); ++i) {
        if (i > 0) {
            out += i + 1 == names.size() ? " and " : ", ";
        }
        out += names[i];
    }
    return out;
}

[[nodiscard]] bool said_yes(std::string answer) {
    while (!answer.empty() && (answer.back() == '\r' || answer.back() == ' ')) {
        answer.pop_back();
    }
    const std::string word = lowered(answer);
    return word == "y" || word == "yes";
}

}  // namespace

std::size_t RegistrationPlan::registers() const noexcept {
    return static_cast<std::size_t>(
        std::ranges::count(steps, RegistrationStep::Outcome::Register, &RegistrationStep::outcome));
}

bool offerable(const backends::ProviderStatus& status) noexcept {
    const backends::ProviderFacts* facts = backends::provider_for_type(status.type);
    if (facts == nullptr) {
        return false;
    }
    if (facts->binary.empty()) {
        return status.tier() >= backends::ProviderTier::CredentialsFound;
    }
    return status.installed;
}

bool has_provider_backend(const harness::Config& config) noexcept {
    return std::ranges::any_of(config.backends, [](const auto& entry) {
        return backends::provider_for_type(entry.second.type) != nullptr;
    });
}

std::vector<backends::ProviderStatus> cached_statuses(const backends::ProviderCache& cache) {
    std::vector<backends::ProviderStatus> out;
    for (const backends::ProviderFacts& facts : backends::provider_table()) {
        if (const auto found = cache.providers.find(facts.id); found != cache.providers.end()) {
            out.push_back(found->second);
        }
    }
    return out;
}

RegistrationPlan plan_registration(const std::vector<backends::ProviderStatus>& statuses,
                                   const harness::Config& config) {
    RegistrationPlan plan;
    for (const backends::ProviderStatus& status : statuses) {
        const backends::ProviderFacts* facts = backends::provider_for_type(status.type);
        if (facts == nullptr || !offerable(status)) {
            continue;
        }
        RegistrationStep step;
        step.provider = std::string{facts->id};
        step.name = std::string{facts->id};
        step.type = facts->type;
        const auto existing = std::ranges::find_if(
            config.backends, [&](const auto& entry) { return entry.second.type == facts->type; });
        if (existing != config.backends.end()) {
            step.outcome = RegistrationStep::Outcome::AlreadyRegistered;
            step.detail = existing->first;
        } else if (facts->needs_model) {
            step.outcome = RegistrationStep::Outcome::NeedsModel;
        } else if (const auto* taken = named(config, step.name); taken != nullptr) {
            step.outcome = RegistrationStep::Outcome::NameTaken;
            step.name = taken->first;
            step.detail = std::string{harness::to_string(taken->second.type)};
        }
        plan.steps.push_back(std::move(step));
    }
    if (config.models.default_backend.empty()) {
        const auto first = std::ranges::find(plan.steps, RegistrationStep::Outcome::Register,
                                             &RegistrationStep::outcome);
        if (first != plan.steps.end()) {
            plan.default_backend = first->name;
        }
    }
    return plan;
}

void apply_registration(const std::filesystem::path& path, const RegistrationPlan& plan) {
    if (plan.registers() == 0) {
        return;
    }
    harness::edit_config_file(path, [&plan](std::string_view content) {
        std::string edited{content};
        for (const RegistrationStep& step : plan.steps) {
            if (step.outcome != RegistrationStep::Outcome::Register) {
                continue;
            }
            harness::BackendConfig entry;
            entry.type = step.type;
            edited = harness::append_backend(edited, step.name, entry, /*force=*/false);
        }
        if (!plan.default_backend.empty()) {
            edited = harness::set_models_role(edited, "default", plan.default_backend);
        }
        return edited;
    });
}

std::string render_registration(const RegistrationPlan& plan, bool applied) {
    std::string out;
    for (const RegistrationStep& step : plan.steps) {
        const std::string type{harness::to_string(step.type)};
        switch (step.outcome) {
            case RegistrationStep::Outcome::Register:
                out +=
                    (applied ? "registered " : "would register ") + step.name + " (" + type + ")\n";
                break;
            case RegistrationStep::Outcome::AlreadyRegistered:
                out += step.provider + ": already registered as '" + step.detail +
                       "' -- nothing written\n";
                break;
            case RegistrationStep::Outcome::NameTaken:
                out += step.provider + ": not registered -- a backend named '" + step.name +
                       "' already exists (" + step.detail + "); add one under another name: " +
                       "apogee config add-backend <name> --type " + type + "\n";
                break;
            case RegistrationStep::Outcome::NeedsModel:
                out += step.provider + ": not registered -- an " + type +
                       " backend needs a model; add one with: apogee config add-backend " +
                       step.name + " --type " + type + " --model <model>\n";
                break;
        }
    }
    if (!plan.default_backend.empty()) {
        out += std::string{applied ? "models.default set to '" : "models.default would be '"} +
               plan.default_backend + "' (none was set)\n";
    }
    if (plan.steps.empty()) {
        out += "nothing to register -- no provider was detected\n";
    } else if (plan.registers() == 0 && applied) {
        out += "nothing written\n";
    }
    return out;
}

OfferOutcome offer_registration(const OfferContext& context, const harness::Config& config,
                                std::istream& in, std::ostream& out) {
    if (!context.interactive || context.machine || context.quiet || has_provider_backend(config)) {
        return OfferOutcome::NotAsked;
    }
    const backends::ProviderCache cache = backends::load_provider_cache(context.cache_path);
    if (!cache.scanned() || !cache.offer_answer.empty()) {
        return OfferOutcome::NotAsked;
    }
    const RegistrationPlan plan = plan_registration(cached_statuses(cache), config);
    std::vector<std::string> names;
    for (const RegistrationStep& step : plan.steps) {
        if (step.outcome == RegistrationStep::Outcome::Register) {
            names.push_back(step.name);
        }
    }
    if (names.empty()) {
        return OfferOutcome::NotAsked;
    }

    out << "Found " << joined(names) << " -- register "
        << (names.size() == 1 ? "it as a backend" : "them as backends") << "? [y/N] " << std::flush;
    std::string answer;
    std::getline(in, answer);
    const bool accepted = said_yes(answer);
    (void)backends::record_offer_answer(context.cache_path, accepted ? "accepted" : "declined",
                                        context.today);
    if (!accepted) {
        out << "not registered -- 'apogee providers scan --register' does it whenever you "
               "like\n";
        return OfferOutcome::Declined;
    }
    apply_registration(context.config_path, plan);
    out << render_registration(plan, /*applied=*/true);
    return OfferOutcome::Registered;
}

}  // namespace apogee::commands
