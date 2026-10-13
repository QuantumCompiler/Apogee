#include "training/regime_specs.h"

#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <random>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "contracts/assets.h"
#include "contracts/config.h"
#include "support/env_guard.h"
#include "training/kit.h"
#include "training/mock_trainer.h"
#include "training/regime.h"

/// The regime specs a run can name (42a): the ladder -- a config entry over
/// a file under training/regimes/ over the shipped spec's compiled-in text,
/// a seeded file told from an edited one by its bytes -- the book's order,
/// its problems, the two documents, and the shipped `foundation` run whole
/// over the four bundled kits on the mock trainer: every cumulative gate
/// passed by a student that answers the evals, and a planted kit the student
/// fails stopping the pipeline at its own stage.
namespace {

using apogee::training::RegimeBook;
using apogee::training::RegimeEntry;
using apogee::training::RegimeSource;

void write(const std::filesystem::path& path, std::string_view content) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream{path, std::ios::binary} << content;
}

[[nodiscard]] std::string shipped_text() {
    const apogee::harness::BundledRegime* regime =
        apogee::harness::find_bundled_regime("foundation");
    REQUIRE(regime != nullptr);
    return std::string{regime->text};
}

struct Fixture {
    apogee::testing::TempDir root{"regime-specs-" + std::to_string(std::random_device{}())};
    std::filesystem::path dir = root.path() / "training" / "regimes";
    apogee::harness::Config config;

    [[nodiscard]] RegimeBook book() const {
        return apogee::training::regime_book(config, dir);
    }
};

/// A candidate that answers every eval prompt it knows with its expected
/// text -- the student the kits' gates are passed by -- and anything else,
/// or a planted prompt, with nothing useful.
class Oracle final : public apogee::training::CandidateRunner {
public:
    Oracle(const std::map<std::string, std::string>* answers, const std::set<std::string>* wrong)
        : answers_{answers}, wrong_{wrong} {}

    [[nodiscard]] apogee::training::CandidateReply run(
        std::string_view prompt, const apogee::harness::CancellationToken&) override {
        const std::string key{prompt};
        if (answers_ == nullptr || wrong_->contains(key) || !answers_->contains(key)) {
            return {.ok = true, .text = "I could not say."};
        }
        return {.ok = true, .text = answers_->at(key)};
    }

private:
    const std::map<std::string, std::string>* answers_;
    const std::set<std::string>* wrong_;
};

/// The mock trainer whose trained candidates are the oracle: training,
/// fusing and the records are the mock's own.
class OracleTrainer final : public apogee::training::Trainer {
public:
    std::map<std::string, std::string> answers;
    std::set<std::string> wrong;

    [[nodiscard]] std::string_view name() const noexcept override {
        return mock_.name();
    }

    [[nodiscard]] apogee::training::TrainerCapabilities capabilities() const override {
        return mock_.capabilities();
    }

    [[nodiscard]] apogee::training::TrainOutcome train(
        const apogee::training::TrainRequest& request,
        const apogee::training::ProgressSink& on_progress,
        const apogee::harness::CancellationToken& cancellation) override {
        return mock_.train(request, on_progress, cancellation);
    }

    [[nodiscard]] std::string fuse(
        const std::filesystem::path& base, const std::filesystem::path& adapter,
        const std::filesystem::path& out, const apogee::training::MessageSink& on_message,
        const apogee::harness::CancellationToken& cancellation) override {
        return mock_.fuse(base, adapter, out, on_message, cancellation);
    }

    [[nodiscard]] std::unique_ptr<apogee::training::CandidateRunner> candidate_runner(
        const std::filesystem::path&, const std::filesystem::path& adapter) override {
        // The untuned base knows nothing.
        return std::make_unique<Oracle>(adapter.empty() ? nullptr : &answers, &wrong);
    }

private:
    apogee::training::MockTrainer mock_;
};

}  // namespace

TEST_CASE(
    "a shipped regime stands from its compiled-in text until a file is there, seeded "
    "while its bytes are the shipped ones and the user's once edited",
    "[training][regimes][ladder]") {
    Fixture fixture;
    RegimeBook book = fixture.book();
    CHECK(book.problems.empty());
    REQUIRE(book.regimes.size() == 1);
    const RegimeEntry* entry = book.find("foundation");
    REQUIRE(entry != nullptr);
    CHECK(entry->source == RegimeSource::Shipped);
    CHECK(entry->path.empty());
    CHECK_FALSE(entry->config);
    CHECK(entry->spec.kits == std::vector<std::string>{"instruction-following", "structured-output",
                                                       "summarization", "reasoning"});
    CHECK(entry->spec.count == 0);  // each kit's own
    CHECK(entry->spec.iters == 0);
    CHECK(entry->spec.teacher.empty());
    CHECK(entry->spec.student.empty());
    CHECK(book.find("nope") == nullptr);

    // Seeded: the shipped bytes at the shipped name.
    write(fixture.dir / "foundation.yaml", shipped_text());
    book = fixture.book();
    entry = book.find("foundation");
    REQUIRE(entry != nullptr);
    CHECK(entry->source == RegimeSource::Seeded);
    CHECK(entry->path == fixture.dir / "foundation.yaml");

    // Edited, it is the user's -- and it is what a run by that name reads.
    write(fixture.dir / "foundation.yaml", shipped_text() + "student: my-student\n");
    book = fixture.book();
    entry = book.find("foundation");
    REQUIRE(entry != nullptr);
    CHECK(entry->source == RegimeSource::User);
    CHECK(entry->spec.student == "my-student");
    CHECK(book.regimes.size() == 1);
}

TEST_CASE(
    "a training.regimes entry outranks a file of its name, and the book lists the shipped "
    "names, then the config's, then the other files",
    "[training][regimes][ladder]") {
    Fixture fixture;
    write(fixture.dir / "foundation.yaml", shipped_text());
    apogee::harness::RegimeSpec mine;
    mine.teacher = "teacher";
    mine.kits = {"reasoning"};
    fixture.config.training.regimes["foundation"] = mine;
    RegimeBook book = fixture.book();
    const RegimeEntry* entry = book.find("foundation");
    REQUIRE(entry != nullptr);
    CHECK(entry->config);
    CHECK(entry->source == RegimeSource::User);
    CHECK(entry->path.empty());
    CHECK(entry->spec.name == "foundation");
    CHECK(entry->spec.kits == std::vector<std::string>{"reasoning"});
    CHECK(book.regimes.size() == 1);  // the file it stands over is not listed twice

    // A config-only entry, and files of the user's own -- .yaml and .yml --
    // in that order after the shipped one; a config entry hides its file.
    fixture.config.training.regimes["zeta"] = mine;
    fixture.config.training.regimes["alpha"] = mine;
    write(fixture.dir / "beta.yml", "kits: [summarization]\n");
    write(fixture.dir / "aardvark.yaml", "kits: [reasoning]\ncount: 3\n");
    write(fixture.dir / "zeta.yaml", "kits: [summarization]\n");
    write(fixture.dir / "notes.txt", "not a spec");
    book = fixture.book();
    CHECK(book.problems.empty());
    CHECK(book.names() == "foundation, alpha, zeta, aardvark, beta");
    REQUIRE(book.find("zeta") != nullptr);
    CHECK(book.find("zeta")->config);
    CHECK(book.find("zeta")->spec.kits == std::vector<std::string>{"reasoning"});
    REQUIRE(book.find("aardvark") != nullptr);
    CHECK(book.find("aardvark")->source == RegimeSource::User);
    CHECK(book.find("aardvark")->spec.count == 3);
    CHECK(book.find("aardvark")->path == fixture.dir / "aardvark.yaml");
    CHECK(book.find("beta")->path == fixture.dir / "beta.yml");
}

TEST_CASE("a spec file that does not parse, or names itself otherwise, is left out and said once",
          "[training][regimes][ladder]") {
    Fixture fixture;
    write(fixture.dir / "broken.yaml", "kits: [unterminated\n");
    write(fixture.dir / "renamed.yaml", "name: other\nkits: [reasoning]\n");
    write(fixture.dir / "fine.yaml", "kits: [reasoning]\n");
    RegimeBook book = fixture.book();
    CHECK(book.names() == "foundation, fine");
    REQUIRE(book.problems.size() == 2);
    CHECK(book.problems[0].find("broken.yaml") != std::string::npos);
    CHECK(book.problems[1].find("names itself 'other'") != std::string::npos);

    // A broken file at a shipped name stands in for the shipped spec: the
    // regime is out, the file said once, never the compiled-in text in its
    // place -- the file is what a run by that name would read.
    write(fixture.dir / "foundation.yaml", "kits: [unterminated\n");
    book = fixture.book();
    CHECK(book.find("foundation") == nullptr);
    REQUIRE(book.problems.size() == 3);
    CHECK(book.problems[0].find("foundation.yaml") != std::string::npos);
}

TEST_CASE("the documents: training.regime for one, the list idiom for the book",
          "[training][regimes][json]") {
    Fixture fixture;
    write(fixture.dir / "mine.yaml",
          "description: mine\nkits: [reasoning]\nteacher: t\nstudent: s\npromote_as: p\n"
          "count: 4\niters: 9\ntemperature: 0.5\n");
    write(fixture.dir / "broken.yaml", "kits: [unterminated\n");
    const RegimeBook book = fixture.book();

    const nlohmann::json shipped = apogee::training::to_json(*book.find("foundation"));
    CHECK(shipped["object"] == "training.regime");
    CHECK(shipped["name"] == "foundation");
    CHECK(shipped["source"] == "shipped");
    CHECK(shipped["config"] == false);
    CHECK(shipped["path"].is_null());
    CHECK(shipped["kits"].size() == 4);
    CHECK(shipped["kits"][0] == "instruction-following");
    CHECK(shipped["count"] == 0);
    CHECK(shipped["teacher"] == "");
    CHECK(shipped["student"] == "");
    CHECK(shipped["promote_as"] == "");
    CHECK_FALSE(shipped["description"].get<std::string>().empty());

    const nlohmann::json mine = apogee::training::to_json(*book.find("mine"));
    CHECK(mine["source"] == "user");
    CHECK(mine["path"] == (fixture.dir / "mine.yaml").string());
    CHECK(mine["description"] == "mine");
    CHECK(mine["teacher"] == "t");
    CHECK(mine["student"] == "s");
    CHECK(mine["promote_as"] == "p");
    CHECK(mine["count"] == 4);
    CHECK(mine["iters"] == 9);
    CHECK(mine["temperature"] == 0.5);

    const nlohmann::json list = apogee::training::to_json(book);
    CHECK(list["object"] == "list");
    REQUIRE(list["data"].size() == 2);
    CHECK(list["data"][0]["name"] == "foundation");
    CHECK_FALSE(list["data"][0].contains("object"));
    CHECK(list["data"][1]["name"] == "mine");
    REQUIRE(list["problems"].size() == 1);
    CHECK(list["problems"][0].get<std::string>().find("broken.yaml") != std::string::npos);
}

TEST_CASE(
    "foundation runs the four bundled kits as one gated pipeline on the mock trainer, and "
    "a planted kit the student fails stops it at that kit's stage",
    "[training][regimes][run]") {
    const apogee::testing::TempDir root{"regime-foundation-" +
                                        std::to_string(std::random_device{}())};
    const std::filesystem::path kits = root.path() / "kits";
    OracleTrainer trainer;
    std::map<std::string, std::vector<std::string>> prompts_of;
    for (const apogee::harness::BundledKit& bundled : apogee::harness::bundled_kits()) {
        const std::filesystem::path path = kits / (std::string{bundled.name} + ".yaml");
        write(path, bundled.text);
        const apogee::training::Kit kit = apogee::training::load_kit(path);
        CHECK(kit.eval.size() >= 24);  // the floor the gate means something at
        for (const apogee::training::EvalItem& item : kit.eval) {
            trainer.answers[item.prompt] = item.expected;
            prompts_of[kit.name].push_back(item.prompt);
        }
    }
    const std::filesystem::path snap = root.path() / "snap";
    write(snap / "config.json", R"({"model_type": "llama"})");
    write(snap / "model.safetensors", "w");

    const apogee::harness::Config config;
    const RegimeBook book = apogee::training::regime_book(config, root.path() / "regimes");
    REQUIRE(book.find("foundation") != nullptr);
    const auto request_for = [&](std::string run_id) {
        apogee::training::RegimeRequest request;
        request.spec = book.find("foundation")->spec;
        request.spec.student = "snap";  // the user's, at run time
        request.kits_dir = kits;
        request.training_dir = root.path() / "training";
        request.regime_run_id = run_id;
        request.work_dir = root.path() / "training" / "regime" / run_id;
        request.base_model = snap;
        request.trainer = &trainer;
        // A teacher that never repeats itself, so each kit's own count is met.
        request.generate = [counter = std::make_shared<int>(0)](
                               std::string_view, std::string_view, double, int,
                               const apogee::harness::CancellationToken&) {
            nlohmann::json examples = nlohmann::json::array();
            for (int i = 0; i < 8; ++i) {
                const std::string n = std::to_string(++*counter);
                examples.push_back({{"prompt", "question " + n}, {"completion", "answer " + n}});
            }
            return apogee::training::GenerateOutcome{.ok = true, .text = examples.dump()};
        };
        return request;
    };

    apogee::training::RegimeOutcome outcome =
        apogee::training::run_regime(request_for("foundation-1"));
    INFO(outcome.error);
    REQUIRE(outcome.ok);
    REQUIRE(outcome.kits.size() == 4);
    for (const apogee::training::RegimeKitResult& kit : outcome.kits) {
        INFO(kit.kit);
        const apogee::training::Kit loaded = apogee::training::load_kit(kits / (kit.kit + ".yaml"));
        CHECK(kit.examples == loaded.synth.count);
    }
    REQUIRE(outcome.pipeline.has_value());
    const apogee::training::PipelineRunManifest& passed = outcome.pipeline->manifest;
    REQUIRE(passed.stages.size() == 4);
    CHECK(passed.stages[0].name == "instruction-following");
    CHECK(passed.stages[1].name == "structured-output");
    CHECK(passed.stages[2].name == "summarization");
    CHECK(passed.stages[3].name == "reasoning");
    for (std::size_t i = 0; i < passed.stages.size(); ++i) {
        INFO(passed.stages[i].name);
        CHECK(passed.stages[i].cumulative_passed);
        REQUIRE(passed.stages[i].eval.has_value());
        // Cumulative: every earlier kit's evals replayed, none skipped.
        CHECK(passed.stages[i].eval->total == static_cast<int>(24 * (i + 1)));
        CHECK(passed.stages[i].eval->num_skipped == 0);
    }
    CHECK(outcome.promote_run_id == "foundation-1-pipe-s3");

    // The planted case: a student that fails summarization's evals passes
    // the first two gates and is stopped at the third, reasoning never run.
    for (const std::string& prompt : prompts_of["summarization"]) {
        trainer.wrong.insert(prompt);
    }
    outcome = apogee::training::run_regime(request_for("foundation-2"));
    CHECK_FALSE(outcome.ok);
    CHECK(outcome.error.find("stage 'summarization' failed the cumulative eval gate") !=
          std::string::npos);
    REQUIRE(outcome.pipeline.has_value());
    const apogee::training::PipelineRunManifest& stopped = outcome.pipeline->manifest;
    REQUIRE(stopped.stages.size() == 4);
    CHECK(stopped.stages[0].cumulative_passed);
    CHECK(stopped.stages[1].cumulative_passed);
    CHECK_FALSE(stopped.stages[2].cumulative_passed);
    CHECK(stopped.stages[2].eval->num_passed == 48);
    CHECK_FALSE(stopped.stages[3].eval.has_value());
}
