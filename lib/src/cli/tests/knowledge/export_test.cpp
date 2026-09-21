#include "knowledge/export.h"

#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

#include <string>
#include <vector>

#include "knowledge/record.h"

/// Sharing: names off, chain on -- and a Markdown report grouped by status
/// that never carries a raw conversation.
namespace {

using apogee::knowledge::Record;

Record make(std::string id, std::string status) {
    Record record;
    record.id = std::move(id);
    record.intent = "the why";
    record.decision = "the what";
    record.status = std::move(status);
    record.discipline = "eng";
    record.downstream_link = "PROJ-1";
    record.provenance.source = "meeting";
    record.provenance.attribution = "Ada Lovelace";
    record.raw_ref = "/home/ada/.apogee/knowledge/raw/" + record.id + ".md";
    record.timestamp = "2026-09-13T12:00:00.000000Z";
    record.supersedes = "kr-older";
    return record;
}

}  // namespace

TEST_CASE("an anonymized export strips the attribution and the local raw_ref and keeps the chain",
          "[knowledge][export][anonymize]") {
    const std::vector<Record> records{make("kr-1", "shipped")};
    const std::vector<Record> shared = apogee::knowledge::export_records(records, true);
    REQUIRE(shared.size() == 1);
    CHECK(shared.front().provenance.attribution.empty());
    CHECK(shared.front().raw_ref.empty());
    CHECK(shared.front().provenance.source == "meeting");
    CHECK(shared.front().downstream_link == "PROJ-1");
    CHECK(shared.front().supersedes == "kr-older");
    CHECK(shared.front().id == "kr-1");
    // The JSON a recipient reads has neither key at all.
    const nlohmann::json json = shared;
    CHECK_FALSE(json[0].contains("raw_ref"));
    CHECK_FALSE(json[0]["provenance"].contains("attribution"));
    CHECK(json[0].contains("supersedes"));

    // A faithful export is unchanged.
    const std::vector<Record> faithful = apogee::knowledge::export_records(records, false);
    CHECK(faithful.front().provenance.attribution == "Ada Lovelace");
    CHECK(faithful.front().raw_ref == records.front().raw_ref);
    CHECK(nlohmann::json(faithful)[0].contains("raw_ref"));
}

TEST_CASE("the Markdown report groups by status in order and omits what is empty",
          "[knowledge][export][markdown]") {
    std::vector<Record> records{make("kr-s1", "shipped"), make("kr-r1", "rejected"),
                                make("kr-s2", "shipped"), make("kr-x1", "superseded"),
                                make("kr-o1", "weird")};
    records[1].provenance.attribution.clear();
    records[1].decision.clear();
    const std::string report = apogee::knowledge::render_markdown(records);
    CHECK(report.starts_with("# Knowledge records\n\n5 record(s).\n"));
    const std::size_t shipped = report.find("\n## shipped (2)\n");
    const std::size_t rejected = report.find("\n## rejected (1)\n");
    const std::size_t superseded = report.find("\n## superseded (1)\n");
    const std::size_t other = report.find("\n## other (1)\n");
    REQUIRE(shipped != std::string::npos);
    REQUIRE(rejected != std::string::npos);
    REQUIRE(superseded != std::string::npos);
    REQUIRE(other != std::string::npos);
    CHECK(shipped < rejected);
    CHECK(rejected < superseded);
    CHECK(superseded < other);
    CHECK(
        report.find("### kr-s1\n\n- **Intent:** the why\n- **Decision:** the what\n- **Status:** "
                    "shipped\n- **Discipline:** eng\n- **Link:** PROJ-1\n- **Source:** meeting\n- "
                    "**Attribution:** Ada Lovelace\n- **Supersedes:** kr-older\n- **Captured:** "
                    "2026-09-13T12:00:00.000000Z\n") != std::string::npos);
    // The rejected record has no attribution and no decision: neither line.
    const std::string rejected_block = report.substr(rejected, superseded - rejected);
    CHECK(rejected_block.find("**Attribution:**") == std::string::npos);
    CHECK(rejected_block.find("**Decision:**") == std::string::npos);
    CHECK(rejected_block.find("### kr-r1") != std::string::npos);
    // The archive path is never in a report.
    CHECK(report.find("raw_ref") == std::string::npos);
    CHECK(report.find("/home/ada") == std::string::npos);
    CHECK(apogee::knowledge::render_markdown({}) == "# Knowledge records\n\n0 record(s).\n");
}
