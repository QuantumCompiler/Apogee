#include "review/sweep.h"

#include <algorithm>
#include <fstream>
#include <iterator>
#include <stdexcept>

#include "contracts/config_edit.h"

namespace apogee::review {
namespace {

[[nodiscard]] std::string names_of(const std::vector<SweepTarget>& targets) {
    std::string out;
    for (const SweepTarget& target : targets) {
        out += (out.empty() ? "" : ", ") + target.name;
    }
    return out;
}

}  // namespace

WatermarkStore::WatermarkStore(std::filesystem::path path) : path_{std::move(path)} {
    std::ifstream in{path_, std::ios::binary};
    if (!in) {
        return;  // no sweep has run: every target is new
    }
    const std::string text{std::istreambuf_iterator<char>{in}, std::istreambuf_iterator<char>{}};
    const nlohmann::json json = nlohmann::json::parse(text, nullptr, false);
    if (json.is_discarded() || !json.is_object()) {
        throw std::runtime_error(path_.string() +
                                 " is not a sweep's watermarks -- move it aside and pass --since "
                                 "to start again");
    }
    for (const auto& [key, value] : json.items()) {
        if (value.is_string()) {
            marks_.emplace(key, value.get<std::string>());
        }
    }
}

std::optional<std::string> WatermarkStore::get(const std::string& key) const {
    const auto it = marks_.find(key);
    return it == marks_.end() ? std::nullopt : std::optional<std::string>{it->second};
}

void WatermarkStore::set(const std::string& key, std::string value) {
    marks_[key] = std::move(value);
    nlohmann::json json = nlohmann::json::object();
    for (const auto& [mark, at] : marks_) {
        json[mark] = at;
    }
    std::filesystem::create_directories(path_.parent_path());
    harness::write_file_atomically(path_, json.dump(2) + "\n");
}

std::string watermark_key(const forge::ForgeProject& project) {
    const forge::MergeRequestRef ref = forge::project_ref(project);
    return ref.host + "/" + ref.project;
}

forge::ForgeProject target_project(const harness::ReviewTargetConfig& target) {
    return forge::ForgeProject{
        .forge = target.forge == "gitlab" ? forge::Forge::GitLab : forge::Forge::GitHub,
        .host = target.host,
        .project = target.repo.empty() ? target.owner : target.repo};
}

nlohmann::json run_sweep(const std::vector<SweepTarget>& targets, WatermarkStore& store,
                         const SweepOptions& options, const ClientFor& client_for,
                         const ReviewOne& review_one, const ProgressFn& progress) {
    for (const std::string& name : options.only) {
        if (std::ranges::none_of(targets,
                                 [&name](const SweepTarget& t) { return t.name == name; })) {
            throw std::runtime_error("no review target named '" + name + "'" +
                                     (targets.empty() ? std::string{" (the config has none)"}
                                                      : " (there are: " + names_of(targets) + ")"));
        }
    }
    const auto say = [&progress](const std::string& line) {
        if (progress) {
            progress(line);
        }
    };
    std::size_t budget = options.cap;
    std::size_t deferred = 0;
    std::size_t reviewed = 0;
    std::size_t posted = 0;
    std::size_t errors = 0;
    nlohmann::json target_docs = nlohmann::json::array();

    for (const SweepTarget& target : targets) {
        if (!options.only.empty() &&
            std::ranges::find(options.only, target.name) == options.only.end()) {
            continue;
        }
        const bool post = target.config.post || options.allow_post;
        nlohmann::json target_doc{{"name", target.name},
                                  {"kind", target.config.repo.empty() ? "owner" : "repo"},
                                  {"grant", target.config.post   ? "target"
                                            : options.allow_post ? "--allow-post"
                                                                 : "none"},
                                  {"projects", nlohmann::json::array()}};
        const forge::ForgeProject named = target_project(target.config);
        std::unique_ptr<forge::ForgeClient> client = client_for(target.config);
        std::vector<std::string> projects;
        if (!target.config.repo.empty()) {
            projects.push_back(target.config.repo);
        } else {
            say(target.name + ": listing " + target.config.owner + "'s repositories");
            try {
                projects = client->owner_projects(named);
            } catch (const forge::ForgeError& e) {
                target_doc["error"] =
                    std::string{e.what()} + (e.remedy().empty() ? "" : " -- " + e.remedy());
                ++errors;
                target_docs.push_back(std::move(target_doc));
                continue;
            }
        }
        for (const std::string& path : projects) {
            const forge::ForgeProject project{
                .forge = named.forge, .host = named.host, .project = path};
            const std::string key = watermark_key(project);
            const std::optional<std::string> before = store.get(key);
            nlohmann::json project_doc{
                {"project", key},
                {"watermark_before",
                 before.has_value() ? nlohmann::json(*before) : nlohmann::json()},
                {"first_sweep", false},
                {"reviewed", nlohmann::json::array()},
                {"skipped_drafts", nlohmann::json::array()}};
            std::string mark;
            if (before.has_value()) {
                mark = *before;
            } else if (options.since.has_value()) {
                mark = *options.since;
                store.set(key, mark);
            } else {
                // History stays unreviewed unless asked: start at now.
                store.set(key, options.now);
                project_doc["first_sweep"] = true;
                project_doc["watermark_after"] = options.now;
                say(key + ": first sweep -- starting at now (--since reviews earlier ones)");
                target_doc["projects"].push_back(std::move(project_doc));
                continue;
            }
            std::vector<forge::MergeRequestListing> listing;
            try {
                listing = client->open_merge_requests(project, mark);
            } catch (const forge::ForgeError& e) {
                project_doc["error"] =
                    std::string{e.what()} + (e.remedy().empty() ? "" : " -- " + e.remedy());
                ++errors;
                target_doc["projects"].push_back(std::move(project_doc));
                continue;
            }
            for (std::size_t i = 0; i < listing.size(); ++i) {
                const forge::MergeRequestListing& item = listing[i];
                if (item.draft && !target.config.drafts) {
                    project_doc["skipped_drafts"].push_back(forge::to_string(item.ref));
                    store.set(key, item.updated_at);
                    continue;
                }
                if (budget == 0) {
                    deferred += listing.size() - i;
                    break;
                }
                --budget;
                say("reviewing " + forge::to_string(item.ref));
                try {
                    nlohmann::json document = review_one(item.ref, target, post);
                    if (document.contains("posted") && document["posted"].value("done", false)) {
                        ++posted;
                    }
                    project_doc["reviewed"].push_back(std::move(document));
                    ++reviewed;
                } catch (const std::exception& e) {
                    // Not advanced past: the next sweep tries it again.
                    project_doc["error"] = forge::to_string(item.ref) + ": " + e.what();
                    ++errors;
                    break;
                }
                store.set(key, item.updated_at);
            }
            const std::optional<std::string> after = store.get(key);
            project_doc["watermark_after"] =
                after.has_value() ? nlohmann::json(*after) : nlohmann::json();
            target_doc["projects"].push_back(std::move(project_doc));
        }
        target_docs.push_back(std::move(target_doc));
    }
    return nlohmann::json{{"object", "review.sweep"},   {"started", options.now},
                          {"dry_run", options.dry_run}, {"allow_post", options.allow_post},
                          {"cap", options.cap},         {"targets", std::move(target_docs)},
                          {"reviewed", reviewed},       {"posted", posted},
                          {"deferred", deferred},       {"errors", errors}};
}

}  // namespace apogee::review
