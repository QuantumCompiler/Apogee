#include "review/sweep.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>
#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>
#include <map>
#include <random>
#include <stdexcept>
#include <string>
#include <vector>

#include "support/env_guard.h"

/// The review sweep over a scripted forge and a recorded review (48d): a
/// first sweep starting at now, `--since` reaching back, the watermark
/// advancing per repository and kept across runs, a crash resuming where it
/// stopped, the cap deferring the rest, drafts skipped unless counted, an
/// owner expanded to its repositories, and the grant -- declared, given at
/// launch, or absent -- passed to every review.
namespace {

using apogee::forge::ForgeProject;
using apogee::forge::MergeRequestListing;
using apogee::review::SweepTarget;
using Catch::Matchers::ContainsSubstring;

/// A forge whose repositories hold the listings a test sets.
class FakeForge final : public apogee::forge::ForgeClient {
public:
    std::map<std::string, std::vector<MergeRequestListing>>* open = nullptr;
    std::map<std::string, std::vector<std::string>>* owners = nullptr;
    std::vector<std::string>* asked = nullptr;

    [[nodiscard]] apogee::forge::Forge forge() const noexcept override {
        return apogee::forge::Forge::GitHub;
    }

    [[nodiscard]] std::vector<MergeRequestListing> open_merge_requests(
        const ForgeProject& project, std::string_view after) override {
        asked->push_back(project.project + " after " + std::string{after});
        std::vector<MergeRequestListing> out;
        for (const MergeRequestListing& item : (*open)[project.project]) {
            if (item.updated_at > after) {
                out.push_back(item);
            }
        }
        return out;
    }

    [[nodiscard]] std::vector<std::string> owner_projects(const ForgeProject& owner) override {
        return (*owners)[owner.project];
    }

    // Unused by the sweep: the review is the closure's.
    [[nodiscard]] apogee::forge::MergeRequest merge_request(
        const apogee::forge::MergeRequestRef&) override {
        throw std::logic_error("unused");
    }

    [[nodiscard]] std::string diff(const apogee::forge::MergeRequestRef&) override {
        throw std::logic_error("unused");
    }

    [[nodiscard]] std::string current_user(const apogee::forge::MergeRequestRef&) override {
        throw std::logic_error("unused");
    }

    [[nodiscard]] std::vector<apogee::forge::PostedComment> comments(
        const apogee::forge::MergeRequestRef&) override {
        throw std::logic_error("unused");
    }

    [[nodiscard]] std::string compare(const apogee::forge::MergeRequestRef&, std::string_view,
                                      std::string_view) override {
        throw std::logic_error("unused");
    }

    void create_comment(const apogee::forge::MergeRequest&,
                        const apogee::forge::InlineComment&) override {
        throw std::logic_error("unused");
    }

    [[nodiscard]] apogee::forge::PublishOutcome publish_batch(
        const apogee::forge::MergeRequest&, const std::vector<apogee::forge::InlineComment>&,
        std::string_view) override {
        throw std::logic_error("unused");
    }
};

[[nodiscard]] MergeRequestListing listing(std::string_view project, std::uint64_t number,
                                          std::string updated, bool draft = false) {
    MergeRequestListing item;
    item.ref = apogee::forge::MergeRequestRef{.forge = apogee::forge::Forge::GitHub,
                                              .host = "github.com",
                                              .project = std::string{project},
                                              .number = number};
    item.updated_at = std::move(updated);
    item.draft = draft;
    return item;
}

[[nodiscard]] SweepTarget repo(std::string name, std::string path, bool post = false) {
    SweepTarget target;
    target.name = std::move(name);
    target.config.repo = std::move(path);
    target.config.post = post;
    return target;
}

/// The world a sweep runs in: a store on disk, the forge's listings, and
/// every review it asked for.
struct World {
    apogee::testing::TempDir dir{"review-sweep-" + std::to_string(std::random_device{}())};
    std::map<std::string, std::vector<MergeRequestListing>> open;
    std::map<std::string, std::vector<std::string>> owners;
    std::vector<std::string> asked;
    std::vector<std::string> reviewed;
    std::vector<bool> grants;
    /// A review of this merge request fails, as a crash would leave it.
    std::uint64_t failing = 0;

    [[nodiscard]] nlohmann::json sweep(const std::vector<SweepTarget>& targets,
                                       apogee::review::SweepOptions options) {
        apogee::review::WatermarkStore store{dir.path() / "watermarks.json"};
        if (options.now.empty()) {
            options.now = "2026-10-18T12:00:00Z";
        }
        const apogee::review::ClientFor client_for = [this](const auto&) {
            auto forge = std::make_unique<FakeForge>();
            forge->open = &open;
            forge->owners = &owners;
            forge->asked = &asked;
            return forge;
        };
        const apogee::review::ReviewOne review_one =
            [this](const apogee::forge::MergeRequestRef& ref, const SweepTarget&, bool post) {
                if (ref.number == failing) {
                    throw std::runtime_error("the model went away");
                }
                reviewed.push_back(apogee::forge::to_string(ref));
                grants.push_back(post);
                return nlohmann::json{{"merge_request", apogee::forge::to_string(ref)},
                                      {"findings", nlohmann::json::array()},
                                      {"posted", {{"done", post}}}};
            };
        return apogee::review::run_sweep(targets, store, options, client_for, review_one);
    }

    [[nodiscard]] std::optional<std::string> mark(const std::string& key) const {
        return apogee::review::WatermarkStore{dir.path() / "watermarks.json"}.get(key);
    }
};

}  // namespace

TEST_CASE("a first sweep starts at now; --since reaches back; the watermark is kept per repository",
          "[review][sweep]") {
    World world;
    world.open["acme/widget"] = {listing("acme/widget", 1, "2026-10-17T09:00:00Z"),
                                 listing("acme/widget", 2, "2026-10-18T09:00:00Z")};
    world.open["acme/gadget"] = {listing("acme/gadget", 5, "2026-10-18T08:00:00Z")};
    const std::vector<SweepTarget> targets{repo("widget", "acme/widget"),
                                           repo("gadget", "acme/gadget")};

    // First sweep: history stays unreviewed -- both start at now.
    const nlohmann::json first = world.sweep(targets, {});
    CHECK(world.reviewed.empty());
    CHECK(first["targets"][0]["projects"][0]["first_sweep"] == true);
    CHECK(world.mark("github.com/acme/widget") == "2026-10-18T12:00:00Z");

    // A target seen first with --since reaches back to it.
    World back;
    back.open = world.open;
    const nlohmann::json since =
        back.sweep({repo("widget", "acme/widget")},
                   apogee::review::SweepOptions{.since = std::string{"2026-10-18T00:00:00Z"}});
    CHECK(back.reviewed == std::vector<std::string>{"github.com/acme/widget#2"});
    CHECK(back.mark("github.com/acme/widget") == "2026-10-18T09:00:00Z");
    CHECK(since["reviewed"] == 1);

    // The next sweep reads past the watermark it left: nothing new, nothing done.
    const nlohmann::json again = back.sweep({repo("widget", "acme/widget")}, {});
    CHECK(back.reviewed.size() == 1);
    CHECK(back.asked.back() == "acme/widget after 2026-10-18T09:00:00Z");
    CHECK(again["targets"][0]["projects"][0]["watermark_before"] == "2026-10-18T09:00:00Z");
}

TEST_CASE("a crash resumes where it stopped, and the cap leaves the rest for the next sweep",
          "[review][sweep]") {
    World world;
    world.open["acme/widget"] = {listing("acme/widget", 1, "2026-10-18T01:00:00Z"),
                                 listing("acme/widget", 2, "2026-10-18T02:00:00Z"),
                                 listing("acme/widget", 3, "2026-10-18T03:00:00Z")};
    const std::vector<SweepTarget> targets{repo("widget", "acme/widget")};
    const apogee::review::SweepOptions since{.since = std::string{"2026-10-17T00:00:00Z"}};

    // #2 fails: #1 is kept, the watermark stops at it, #3 waits.
    world.failing = 2;
    const nlohmann::json crashed = world.sweep(targets, since);
    CHECK(world.reviewed == std::vector<std::string>{"github.com/acme/widget#1"});
    CHECK(world.mark("github.com/acme/widget") == "2026-10-18T01:00:00Z");
    CHECK(crashed["errors"] == 1);
    CHECK_THAT(crashed["targets"][0]["projects"][0]["error"].get<std::string>(),
               ContainsSubstring("github.com/acme/widget#2: the model went away"));

    // The next sweep resumes at #2 -- never #1 again.
    world.failing = 0;
    apogee::review::SweepOptions capped;
    capped.cap = 1;
    const nlohmann::json resumed = world.sweep(targets, capped);
    CHECK(world.reviewed ==
          std::vector<std::string>{"github.com/acme/widget#1", "github.com/acme/widget#2"});
    CHECK(resumed["deferred"] == 1);  // #3, past the cap
    CHECK(world.mark("github.com/acme/widget") == "2026-10-18T02:00:00Z");
    (void)world.sweep(targets, {});
    CHECK(world.reviewed.back() == "github.com/acme/widget#3");
}

TEST_CASE("drafts skip unless counted; an owner expands; the grant reaches each review",
          "[review][sweep]") {
    World world;
    world.owners["acme"] = {"acme/one", "acme/two"};
    world.open["acme/one"] = {listing("acme/one", 1, "2026-10-18T01:00:00Z", /*draft=*/true),
                              listing("acme/one", 2, "2026-10-18T02:00:00Z")};
    world.open["acme/two"] = {listing("acme/two", 3, "2026-10-18T03:00:00Z")};
    SweepTarget owner;
    owner.name = "acme";
    owner.config.owner = "acme";
    const apogee::review::SweepOptions since{.since = std::string{"2026-10-17T00:00:00Z"}};

    const nlohmann::json swept = world.sweep({owner}, since);
    CHECK(world.reviewed ==
          std::vector<std::string>{"github.com/acme/one#2", "github.com/acme/two#3"});
    CHECK(swept["targets"][0]["kind"] == "owner");
    CHECK(swept["targets"][0]["projects"].size() == 2);
    CHECK(swept["targets"][0]["projects"][0]["skipped_drafts"] ==
          nlohmann::json::array({"github.com/acme/one#1"}));
    // No grant: every review is told it may not post.
    CHECK(swept["targets"][0]["grant"] == "none");
    CHECK(world.grants == std::vector<bool>{false, false});
    CHECK(swept["posted"] == 0);

    // A target counting drafts reviews them; --allow-post grants the run.
    World drafts;
    drafts.open = world.open;
    SweepTarget counted = repo("one", "acme/one");
    counted.config.drafts = true;
    apogee::review::SweepOptions granted = since;
    granted.allow_post = true;
    const nlohmann::json posted = drafts.sweep({counted, repo("two", "acme/two", true)}, granted);
    CHECK(drafts.reviewed == std::vector<std::string>{"github.com/acme/one#1",
                                                      "github.com/acme/one#2",
                                                      "github.com/acme/two#3"});
    CHECK(drafts.grants == std::vector<bool>{true, true, true});
    CHECK(posted["targets"][0]["grant"] == "--allow-post");
    CHECK(posted["targets"][1]["grant"] == "target");
    CHECK(posted["posted"] == 3);

    // --target names one; a name the config lacks is refused naming the rest.
    World one;
    one.open = world.open;
    (void)one.sweep({repo("one", "acme/one"), repo("two", "acme/two")},
                    apogee::review::SweepOptions{.only = {"two"},
                                                 .since = std::string{"2026-10-17T00:00:00Z"}});
    CHECK(one.reviewed == std::vector<std::string>{"github.com/acme/two#3"});
    CHECK_THROWS_WITH(
        one.sweep({repo("one", "acme/one")}, apogee::review::SweepOptions{.only = {"nope"}}),
        ContainsSubstring("no review target named 'nope' (there are: one)"));
}

TEST_CASE("a watermark file that is not the store's is refused, saying how to start again",
          "[review][sweep]") {
    const apogee::testing::TempDir dir{"review-marks-" + std::to_string(std::random_device{}())};
    std::ofstream{dir.path() / "watermarks.json"} << "not json";
    CHECK_THROWS_WITH(apogee::review::WatermarkStore{dir.path() / "watermarks.json"},
                      ContainsSubstring("is not a sweep's watermarks"));
    CHECK(apogee::review::watermark_key(
              ForgeProject{.forge = apogee::forge::Forge::GitLab, .host = "", .project = "g/p"}) ==
          "gitlab.com/g/p");
}
