#include "knowledge/record.h"

#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <chrono>
#include <set>
#include <string>

/// The canonical record: normalisation, validation, the thin index, the
/// anonymised copy, the id, the JSON shape, and the one decoder of a chunk.
namespace {

using apogee::knowledge::Record;

Record sample() {
    Record record;
    record.id = "kr-20260913T120000Z-abc123";
    record.intent = "We dropped the cancel button because nobody used it and it confused testers.";
    record.decision = "Remove the cancel button from the checkout page.";
    record.status = "shipped";
    record.discipline = "ux";
    record.downstream_link = "PROJ-42";
    record.provenance.source = "meeting";
    record.provenance.attribution = "Ada Lovelace";
    record.raw_ref = "/home/x/.apogee/knowledge/raw/kr-20260913T120000Z-abc123.md";
    record.timestamp = "2026-09-13T12:00:00.000000Z";
    record.supersedes = "kr-20260901T090000Z-000001";
    return record;
}

}  // namespace

TEST_CASE("normalize trims, canonicalises, and defaults the source", "[knowledge][record]") {
    Record record;
    record.intent = "  why  \n";
    record.decision = "\twhat ";
    record.status = " Live ";
    record.discipline = " UX";
    record.downstream_link = " T-1 ";
    record.provenance.attribution = " Ada ";
    record.supersedes = " kr-x ";
    apogee::knowledge::normalize(record);
    CHECK(record.intent == "why");
    CHECK(record.decision == "what");
    CHECK(record.status == "shipped");
    CHECK(record.discipline == "ux");
    CHECK(record.downstream_link == "T-1");
    CHECK(record.provenance.attribution == "Ada");
    CHECK(record.supersedes == "kr-x");
    // No source named: the default, so a record always says where it came from.
    CHECK(record.provenance.source == "manual");

    Record named;
    named.provenance.source = " chat ";
    apogee::knowledge::normalize(named);
    CHECK(named.provenance.source == "chat");
}

TEST_CASE("status synonyms fold onto the canonical set; unknown words stay unknown",
          "[knowledge][record][status]") {
    using apogee::knowledge::normalize_status;

    struct Case {
        const char* in;
        const char* want;
    };

    const Case cases[] = {
        {"shipped", "shipped"},
        {"SHIP", "shipped"},
        {"live", "shipped"},
        {"Done", "shipped"},
        {"implemented", "shipped"},
        {"rejected", "rejected"},
        {"reject", "rejected"},
        {"abandoned", "rejected"},
        {"dropped", "rejected"},
        {"killed", "rejected"},
        {"discarded", "rejected"},
        {"superseded", "superseded"},
        {"supersede", "superseded"},
        {"Replaced", "superseded"},
        {"obsolete", "superseded"},
        {"  maybe  ", "maybe"},
        {"", ""},
    };
    for (const Case& c : cases) {
        INFO(c.in);
        CHECK(normalize_status(c.in) == c.want);
    }
    CHECK(apogee::knowledge::is_valid_status("shipped"));
    CHECK(apogee::knowledge::is_valid_status("rejected"));
    CHECK(apogee::knowledge::is_valid_status("superseded"));
    CHECK_FALSE(apogee::knowledge::is_valid_status("maybe"));
    CHECK_FALSE(apogee::knowledge::is_valid_status("Shipped"));
    CHECK(apogee::knowledge::valid_statuses().size() == 3);
    CHECK(apogee::knowledge::valid_disciplines().size() == 5);
}

TEST_CASE("validate refuses an empty intent and a non-canonical status", "[knowledge][record]") {
    Record record = sample();
    CHECK(apogee::knowledge::validate(record).empty());

    record.intent = "   ";
    CHECK(apogee::knowledge::validate(record).find("intent") != std::string::npos);

    record = sample();
    record.status = "maybe";
    const std::string why = apogee::knowledge::validate(record);
    CHECK(why.find("maybe") != std::string::npos);
    CHECK(why.find("shipped, rejected, superseded") != std::string::npos);

    // The synonym is not the canonical word until normalised.
    record.status = "live";
    CHECK_FALSE(apogee::knowledge::validate(record).empty());
    apogee::knowledge::normalize(record);
    CHECK(apogee::knowledge::validate(record).empty());
}

TEST_CASE("the index text is the intent and the decision, and nothing that changes or names",
          "[knowledge][record][index]") {
    const Record record = sample();
    const std::string text = apogee::knowledge::index_text(record);
    CHECK(text.starts_with(record.intent));
    CHECK(text.find("\n\nDecision: " + record.decision) != std::string::npos);
    // Attribution can never be surfaced by a query; the link and the status
    // are metadata to filter over, not content to search for.
    CHECK(text.find("Ada") == std::string::npos);
    CHECK(text.find("PROJ-42") == std::string::npos);
    CHECK(text.find("shipped") == std::string::npos);
    CHECK(text.find("meeting") == std::string::npos);
    CHECK(text.find(record.id) == std::string::npos);

    Record bare = record;
    bare.decision.clear();
    CHECK(apogee::knowledge::index_text(bare) == record.intent);
}

TEST_CASE("anonymize strips the attribution and keeps every chain field",
          "[knowledge][record][anonymize]") {
    const Record record = sample();
    const Record anonymous = apogee::knowledge::anonymize(record);
    CHECK(anonymous.provenance.attribution.empty());
    CHECK(anonymous.provenance.source == record.provenance.source);
    CHECK(anonymous.downstream_link == record.downstream_link);
    CHECK(anonymous.supersedes == record.supersedes);
    CHECK(anonymous.raw_ref == record.raw_ref);
    CHECK(anonymous.id == record.id);
    CHECK(anonymous.intent == record.intent);
    // A copy: the original keeps its name.
    CHECK(record.provenance.attribution == "Ada Lovelace");
}

TEST_CASE("ids are kr-<UTC second>-<6 hex>, sortable by time, and unique within a second",
          "[knowledge][record][id]") {
    const auto when = std::chrono::system_clock::from_time_t(1789300000);  // 2026-09-13-ish, UTC
    const std::string id = apogee::knowledge::new_id(when);
    REQUIRE(id.size() == 3 + 16 + 1 + 6);
    CHECK(id.starts_with("kr-"));
    CHECK(id[11] == 'T');
    CHECK(id[18] == 'Z');
    CHECK(id[19] == '-');
    for (std::size_t i = 20; i < id.size(); ++i) {
        CHECK(std::isxdigit(static_cast<unsigned char>(id[i])) != 0);
    }
    std::set<std::string> seen;
    for (int i = 0; i < 200; ++i) {
        seen.insert(apogee::knowledge::new_id(when));
    }
    CHECK(seen.size() == 200);
    // Later captures sort later, whatever the suffix.
    const std::string later = apogee::knowledge::new_id(when + std::chrono::seconds{1});
    CHECK(id < later);
    CHECK(id.substr(3, 16) < later.substr(3, 16));

    const std::string stamp = apogee::knowledge::timestamp_for(when);
    CHECK(stamp.starts_with(id.substr(3, 4) + "-" + id.substr(7, 2) + "-" + id.substr(9, 2) + "T"));
    CHECK(stamp.ends_with("Z"));
    CHECK(stamp.size() == 27);
    CHECK(apogee::knowledge::timestamp_for(when + std::chrono::microseconds{5}) > stamp);
}

TEST_CASE("the JSON shape is Ommi's: optional fields omitted when empty, and it round-trips",
          "[knowledge][record][json]") {
    const Record record = sample();
    const nlohmann::json json = record;
    CHECK(json["id"] == record.id);
    CHECK(json["intent"] == record.intent);
    CHECK(json["decision"] == record.decision);
    CHECK(json["status"] == "shipped");
    CHECK(json["discipline"] == "ux");
    CHECK(json["downstream_link"] == "PROJ-42");
    CHECK(json["provenance"]["source"] == "meeting");
    CHECK(json["provenance"]["attribution"] == "Ada Lovelace");
    CHECK(json["raw_ref"] == record.raw_ref);
    CHECK(json["timestamp"] == record.timestamp);
    CHECK(json["supersedes"] == record.supersedes);

    const Record back = json.get<Record>();
    CHECK(back.id == record.id);
    CHECK(back.intent == record.intent);
    CHECK(back.decision == record.decision);
    CHECK(back.status == record.status);
    CHECK(back.discipline == record.discipline);
    CHECK(back.downstream_link == record.downstream_link);
    CHECK(back.provenance.source == record.provenance.source);
    CHECK(back.provenance.attribution == record.provenance.attribution);
    CHECK(back.raw_ref == record.raw_ref);
    CHECK(back.timestamp == record.timestamp);
    CHECK(back.supersedes == record.supersedes);

    // Names off, chain on -- and the omitted keys are ABSENT, not empty.
    const nlohmann::json anonymous = apogee::knowledge::anonymize(record);
    CHECK_FALSE(anonymous["provenance"].contains("attribution"));
    Record thin = record;
    thin.raw_ref.clear();
    thin.supersedes.clear();
    const nlohmann::json thin_json = thin;
    CHECK_FALSE(thin_json.contains("raw_ref"));
    CHECK_FALSE(thin_json.contains("supersedes"));
    CHECK(thin_json.contains("downstream_link"));

    // A partial object reads with defaults, never throws.
    const Record partial = nlohmann::json::parse(R"({"intent": "why"})").get<Record>();
    CHECK(partial.intent == "why");
    CHECK(partial.status.empty());
    CHECK(partial.provenance.source.empty());
    const Record wrong = nlohmann::json::parse(R"({"intent": 7, "provenance": "x"})").get<Record>();
    CHECK(wrong.intent.empty());
}

TEST_CASE("the chunk decoder is strict: keyed by its own id, with an intent, or nothing",
          "[knowledge][record][decoder]") {
    using apogee::knowledge::record_from_metadata;
    const Record record = sample();
    const std::string metadata = nlohmann::json(record).dump();
    std::string error;

    const std::optional<Record> decoded = record_from_metadata(record.id, metadata, error);
    REQUIRE(decoded.has_value());
    CHECK(error.empty());
    CHECK(decoded->intent == record.intent);
    CHECK(decoded->provenance.attribution == record.provenance.attribution);

    CHECK_FALSE(record_from_metadata(record.id, "", error).has_value());
    CHECK(error.find("no record metadata") != std::string::npos);
    CHECK_FALSE(record_from_metadata(record.id, "   ", error).has_value());
    CHECK_FALSE(record_from_metadata(record.id, "not json", error).has_value());
    CHECK(error.find("not a record") != std::string::npos);
    CHECK_FALSE(record_from_metadata(record.id, "[1,2]", error).has_value());
    // An ordinary chunk that happens to carry some other JSON is not a record.
    CHECK_FALSE(record_from_metadata("notes.md", R"({"kind": "note"})", error).has_value());
    CHECK(error.find("keyed by its own id") != std::string::npos);
    // A record under someone else's source is not that source's record.
    CHECK_FALSE(record_from_metadata("kr-other", metadata, error).has_value());
    Record no_intent = record;
    no_intent.intent = " ";
    CHECK_FALSE(
        record_from_metadata(record.id, nlohmann::json(no_intent).dump(), error).has_value());
    CHECK(error.find("no intent") != std::string::npos);
}
