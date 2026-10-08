#include "cli/provider_offer.h"

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <iterator>
#include <random>
#include <sstream>
#include <string>
#include <vector>

#include "backends/provider_cache.h"
#include "backends/provider_table.h"
#include "cli/providers_cmd.h"
#include "contracts/config.h"
#include "contracts/config_edit.h"
#include "support/env_guard.h"

/// Registration offers (28b): the pass `providers scan --register` runs and
/// the one-time offer, against hand-built scan results and real config files
/// in a temp directory -- no provider is probed by any case here.
namespace {

using apogee::backends::CredentialState;
using apogee::backends::ProviderCache;
using apogee::backends::ProviderStatus;
using apogee::commands::OfferContext;
using apogee::commands::OfferOutcome;
using apogee::commands::RegistrationPlan;
using apogee::commands::RegistrationStep;
using apogee::harness::BackendType;
using Outcome = RegistrationStep::Outcome;

constexpr const char* kConfig = R"(# my own notes, kept through every edit
backends:
  # the local one
  mock:
    type: mock
    model: mock-1

models:
  default: mock
)";

[[nodiscard]] ProviderStatus status(std::string_view id, bool installed,
                                    CredentialState credentials = CredentialState::Unknown) {
    const apogee::backends::ProviderFacts* facts = apogee::backends::find_provider(id);
    REQUIRE(facts != nullptr);
    ProviderStatus out;
    out.id = std::string{id};
    out.type = facts->type;
    out.installed = installed;
    out.credentials = credentials;
    out.installed_evidence = installed ? "/bin/" + std::string{id} + ", 1.0" : "not on PATH";
    return out;
}

/// Every provider, as a scan of a machine with `found` installed would list
/// them.
[[nodiscard]] std::vector<ProviderStatus> scan_with(const std::vector<std::string>& found) {
    std::vector<ProviderStatus> out;
    for (const auto& facts : apogee::backends::provider_table()) {
        const bool here = std::ranges::find(found, facts.id) != found.end();
        out.push_back(status(
            facts.id, here,
            here && facts.binary.empty() ? CredentialState::Found : CredentialState::Unknown));
    }
    return out;
}

[[nodiscard]] apogee::harness::Config config_of(const std::string& text) {
    return apogee::harness::parse_config(text, "<test>");
}

[[nodiscard]] std::string read(const std::filesystem::path& path) {
    std::ifstream in{path, std::ios::binary};
    return {std::istreambuf_iterator<char>{in}, std::istreambuf_iterator<char>{}};
}

struct Sandbox {
    apogee::testing::TempDir dir{"provider-offer-" + std::to_string(std::random_device{}())};
    std::filesystem::path config = dir.path() / "config" / "config.yaml";
    std::filesystem::path cache = dir.path() / "cache" / "providers.json";

    explicit Sandbox(const std::string& text = kConfig) {
        std::filesystem::create_directories(config.parent_path());
        std::ofstream{config, std::ios::binary} << text;
    }

    void scanned(const std::vector<std::string>& found) const {
        ProviderCache cache_state;
        cache_state.scanned_at = "2026-10-07T12:00:00Z";
        for (ProviderStatus& row : scan_with(found)) {
            cache_state.providers.emplace(row.id, std::move(row));
        }
        REQUIRE(apogee::backends::store_provider_cache(cache, cache_state));
    }

    [[nodiscard]] OfferContext context() const {
        OfferContext out;
        out.interactive = true;
        out.config_path = config;
        out.cache_path = cache;
        out.today = "2026-10-07";
        return out;
    }

    [[nodiscard]] OfferOutcome offer(const std::string& answer, std::string* said,
                                     OfferContext context) const {
        std::istringstream in{answer};
        std::ostringstream out;
        const OfferOutcome outcome = apogee::commands::offer_registration(
            context, apogee::harness::load_config(config), in, out);
        *said = out.str();
        return outcome;
    }

    [[nodiscard]] OfferOutcome offer(const std::string& answer, std::string* said) const {
        return offer(answer, said, context());
    }
};

}  // namespace

TEST_CASE("the plan: one entry per detected provider that has none", "[providers][register]") {
    const auto config = config_of(kConfig);

    SECTION("nothing detected: nothing to do") {
        const RegistrationPlan plan = apogee::commands::plan_registration(scan_with({}), config);
        CHECK(plan.steps.empty());
        CHECK(plan.registers() == 0);
        CHECK(apogee::commands::render_registration(plan, true) ==
              "nothing to register -- no provider was detected\n");
    }
    SECTION("two CLIs and a key: three entries, by their short names") {
        const RegistrationPlan plan = apogee::commands::plan_registration(
            scan_with({"claude", "codex", "anthropic"}), config);
        REQUIRE(plan.steps.size() == 3);
        CHECK(plan.registers() == 3);
        CHECK(plan.steps[0].name == "claude");
        CHECK(plan.steps[0].type == BackendType::ClaudeCli);
        CHECK(plan.steps[1].name == "codex");
        CHECK(plan.steps[2].name == "anthropic");
        CHECK(plan.default_backend.empty());  // mock is the default already
    }
    SECTION("an API type needs its key to resolve; a CLI only needs to be installed") {
        std::vector<ProviderStatus> rows = scan_with({});
        ProviderStatus& openai = rows[5];
        REQUIRE(openai.id == "openai");
        openai.installed = true;  // below the credentials tier: not offered
        openai.credentials = CredentialState::NotFound;
        CHECK(apogee::commands::plan_registration(rows, config).steps.empty());
        const RegistrationPlan cli =
            apogee::commands::plan_registration(scan_with({"gemini"}), config);
        REQUIRE(cli.steps.size() == 1);
        CHECK(cli.steps[0].outcome == Outcome::Register);
    }
    SECTION("ollama is said, never registered without a model") {
        const RegistrationPlan plan =
            apogee::commands::plan_registration(scan_with({"ollama"}), config);
        REQUIRE(plan.steps.size() == 1);
        CHECK(plan.steps[0].outcome == Outcome::NeedsModel);
        CHECK(plan.registers() == 0);
        CHECK(apogee::commands::render_registration(plan, true) ==
              "ollama: not registered -- an ollama-cli backend needs a model; add one with: "
              "apogee config add-backend ollama --type ollama-cli --model <model>\n"
              "nothing written\n");
    }
    SECTION("a provider already reached by a backend is skipped, said by name") {
        const auto with = config_of(R"(backends:
  work-claude:
    type: claude-cli
)");
        const RegistrationPlan plan =
            apogee::commands::plan_registration(scan_with({"claude", "codex"}), with);
        REQUIRE(plan.steps.size() == 2);
        CHECK(plan.steps[0].outcome == Outcome::AlreadyRegistered);
        CHECK(plan.steps[0].detail == "work-claude");
        CHECK(plan.steps[1].outcome == Outcome::Register);
        // No default yet: the first written entry becomes it, and says so.
        CHECK(plan.default_backend == "codex");
        CHECK(apogee::commands::render_registration(plan, true) ==
              "claude: already registered as 'work-claude' -- nothing written\n"
              "registered codex (codex-cli)\n"
              "models.default set to 'codex' (none was set)\n");
    }
    SECTION("a name taken by another type is skipped, never suffixed -- case aside") {
        const auto taken = config_of(R"(backends:
  Claude:
    type: mock
)");
        const RegistrationPlan plan =
            apogee::commands::plan_registration(scan_with({"claude"}), taken);
        REQUIRE(plan.steps.size() == 1);
        CHECK(plan.steps[0].outcome == Outcome::NameTaken);
        CHECK(plan.steps[0].name == "Claude");
        CHECK(plan.steps[0].detail == "mock");
        CHECK(plan.default_backend.empty());
    }
}

TEST_CASE("register writes through the editor: the user's bytes kept, a second pass a no-op",
          "[providers][register]") {
    const Sandbox box;
    const std::string before = read(box.config);
    const auto statuses = scan_with({"claude", "codex"});

    const RegistrationPlan plan =
        apogee::commands::plan_registration(statuses, apogee::harness::load_config(box.config));
    apogee::commands::apply_registration(box.config, plan);
    const std::string after = read(box.config);

    // Exactly what `config add-backend` would have written, twice.
    apogee::harness::BackendConfig claude;
    claude.type = BackendType::ClaudeCli;
    apogee::harness::BackendConfig codex;
    codex.type = BackendType::CodexCli;
    const std::string expected = apogee::harness::append_backend(
        apogee::harness::append_backend(before, "claude", claude, false), "codex", codex, false);
    CHECK(after == expected);
    CHECK(after.starts_with("# my own notes, kept through every edit\n"));
    CHECK(after.find("  # the local one\n") != std::string::npos);

    const apogee::harness::Config loaded = apogee::harness::load_config(box.config);
    CHECK(loaded.find_backend("claude")->type == BackendType::ClaudeCli);
    CHECK(loaded.find_backend("codex")->model.empty());  // the CLI picks its own default
    CHECK(loaded.models.default_backend == "mock");

    const RegistrationPlan again = apogee::commands::plan_registration(statuses, loaded);
    CHECK(again.registers() == 0);
    apogee::commands::apply_registration(box.config, again);
    CHECK(read(box.config) == after);
    CHECK(apogee::commands::render_registration(again, true) ==
          "claude: already registered as 'claude' -- nothing written\n"
          "codex: already registered as 'codex' -- nothing written\n"
          "nothing written\n");
}

TEST_CASE("the offer: asked once ever, interactive only, declining writes nothing",
          "[providers][offer]") {
    std::string said;

    SECTION("declined: the config byte-identical, the answer kept, never asked again") {
        const Sandbox box;
        box.scanned({"claude", "codex", "gemini"});
        const std::string before = read(box.config);
        CHECK(box.offer("n\n", &said) == OfferOutcome::Declined);
        CHECK(
            said.starts_with("Found claude, codex and gemini -- register them as backends? "
                             "[y/N] "));
        CHECK(said.find("'apogee providers scan --register'") != std::string::npos);
        CHECK(read(box.config) == before);
        const ProviderCache cache = apogee::backends::load_provider_cache(box.cache);
        CHECK(cache.offer_answer == "declined");
        CHECK(cache.offer_date == "2026-10-07");
        CHECK(cache.scanned());  // the scan it offered from is kept

        CHECK(box.offer("y\n", &said) == OfferOutcome::NotAsked);
        CHECK(said.empty());
        CHECK(read(box.config) == before);
    }
    SECTION("an empty answer, or none at all, is a no") {
        const Sandbox box;
        box.scanned({"claude"});
        CHECK(box.offer("\n", &said) == OfferOutcome::Declined);
        CHECK(said.starts_with("Found claude -- register it as a backend? [y/N] "));
        const Sandbox eof;
        eof.scanned({"claude"});
        CHECK(eof.offer("", &said) == OfferOutcome::Declined);
    }
    SECTION("accepted: the entries written and named, never asked again") {
        const Sandbox box;
        box.scanned({"claude", "codex", "ollama"});
        CHECK(box.offer("Yes\n", &said) == OfferOutcome::Registered);
        CHECK(said.find("Found claude and codex -- register them as backends?") == 0);
        CHECK(said.find("registered claude (claude-cli)\nregistered codex (codex-cli)\n") !=
              std::string::npos);
        CHECK(said.find("ollama: not registered") != std::string::npos);
        const apogee::harness::Config loaded = apogee::harness::load_config(box.config);
        CHECK(loaded.find_backend("claude") != nullptr);
        CHECK(loaded.find_backend("codex") != nullptr);
        CHECK(apogee::backends::load_provider_cache(box.cache).offer_answer == "accepted");
        CHECK(box.offer("y\n", &said) == OfferOutcome::NotAsked);
    }
    SECTION("no person to answer: never asked, nothing recorded") {
        const Sandbox box;
        box.scanned({"claude"});
        const std::string before = read(box.config);
        for (const auto mutate : {+[](OfferContext& c) { c.interactive = false; },
                                  +[](OfferContext& c) { c.machine = true; },
                                  +[](OfferContext& c) { c.quiet = true; }}) {
            OfferContext context = box.context();
            mutate(context);
            CHECK(box.offer("y\n", &said, context) == OfferOutcome::NotAsked);
            CHECK(said.empty());
        }
        CHECK(read(box.config) == before);
        CHECK(apogee::backends::load_provider_cache(box.cache).offer_answer.empty());
    }
    SECTION("a provider backend already configured: no offer") {
        const Sandbox box{"backends:\n  mock:\n    type: mock\n  mine:\n    type: gemini-cli\n"};
        box.scanned({"claude"});
        CHECK(box.offer("y\n", &said) == OfferOutcome::NotAsked);
    }
    SECTION("never scanned, or nothing offerable: no offer, nothing recorded") {
        const Sandbox box;
        CHECK(box.offer("y\n", &said) == OfferOutcome::NotAsked);
        CHECK_FALSE(std::filesystem::exists(box.cache));
        box.scanned({"ollama"});  // detected, but not registrable without a model
        CHECK(box.offer("y\n", &said) == OfferOutcome::NotAsked);
        CHECK(apogee::backends::load_provider_cache(box.cache).offer_answer.empty());
    }
}

TEST_CASE("the scan as printed: tier words and evidence, never 'authenticated'",
          "[providers][scan]") {
    std::vector<ProviderStatus> rows = scan_with({"claude", "anthropic"});
    rows[0].credentials = CredentialState::Found;
    rows[0].credential_evidence = "~/.claude.json exists";
    rows[1].installed_evidence = "'codex' is not on PATH";
    rows[4].installed_evidence = "key from ANTHROPIC_API_KEY";
    rows[4].credential_evidence = "key from ANTHROPIC_API_KEY";
    rows[2].installed = true;
    rows[2].installed_evidence = "/bin/gemini, 0.9";
    rows[2].credentials = CredentialState::Found;
    rows[2].verified = apogee::backends::VerifiedRecord{"2026-10-06", "gem"};
    const auto config = config_of(R"(backends:
  gem:
    type: gemini-cli
)");
    const std::string text = apogee::commands::render_provider_scan(rows, &config);
    CHECK(
        text.starts_with("claude     credentials found     not registered\n"
                         "           /bin/claude, 1.0\n"
                         "           ~/.claude.json exists\n"
                         "codex      not found\n"
                         "           'codex' is not on PATH\n"
                         "gemini     verified 2026-10-06   backend: gem\n"
                         "           /bin/gemini, 0.9\n"
                         "           answered a turn on 2026-10-06 (gem)\n"));
    CHECK(text.find("anthropic  credentials found     not registered\n"
                    "           key from ANTHROPIC_API_KEY\n"
                    "openai") != std::string::npos);
    CHECK(text.find("authenticated") == std::string::npos);
    // With no config file, nothing is said about backends.
    CHECK(apogee::commands::render_provider_scan(rows, nullptr)
              .starts_with("claude     credentials found\n"));
}
