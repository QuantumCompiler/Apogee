#include "knowledge/record.h"

#include <nlohmann/json.hpp>

#include <array>
#include <cctype>
#include <ctime>
#include <iomanip>
#include <random>
#include <sstream>

namespace apogee::knowledge {
namespace {

constexpr std::array<std::string_view, 3> kStatuses{kStatusShipped, kStatusRejected,
                                                    kStatusSuperseded};
constexpr std::array<std::string_view, 5> kDisciplines{"program", "product", "project", "ux",
                                                       "eng"};

[[nodiscard]] std::string trim(std::string_view text) {
    std::size_t begin = 0;
    while (begin < text.size() && std::isspace(static_cast<unsigned char>(text[begin])) != 0) {
        ++begin;
    }
    std::size_t end = text.size();
    while (end > begin && std::isspace(static_cast<unsigned char>(text[end - 1])) != 0) {
        --end;
    }
    return std::string{text.substr(begin, end - begin)};
}

[[nodiscard]] std::string lower(std::string text) {
    for (char& c : text) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return text;
}

[[nodiscard]] std::tm utc_parts(std::chrono::system_clock::time_point now) {
    const std::time_t seconds = std::chrono::system_clock::to_time_t(now);
    std::tm utc{};
#if defined(_WIN32)
    gmtime_s(&utc, &seconds);
#else
    gmtime_r(&seconds, &utc);
#endif
    return utc;
}

[[nodiscard]] std::string string_or_empty(const nlohmann::json& in, std::string_view key) {
    const auto it = in.find(key);
    if (it == in.end() || !it->is_string()) {
        return {};
    }
    return it->get<std::string>();
}

}  // namespace

std::span<const std::string_view> valid_statuses() noexcept {
    return kStatuses;
}

std::span<const std::string_view> valid_disciplines() noexcept {
    return kDisciplines;
}

bool is_valid_status(std::string_view status) noexcept {
    for (const std::string_view candidate : kStatuses) {
        if (candidate == status) {
            return true;
        }
    }
    return false;
}

std::string normalize_status(std::string_view status) {
    const std::string word = lower(trim(status));
    if (word == "shipped" || word == "ship" || word == "live" || word == "done" ||
        word == "implemented") {
        return std::string{kStatusShipped};
    }
    if (word == "rejected" || word == "reject" || word == "abandoned" || word == "dropped" ||
        word == "killed" || word == "discarded") {
        return std::string{kStatusRejected};
    }
    if (word == "superseded" || word == "supersede" || word == "replaced" || word == "obsolete") {
        return std::string{kStatusSuperseded};
    }
    return word;
}

std::string new_id(std::chrono::system_clock::time_point now) {
    // Three random bytes -- six hex digits -- from a generator seeded once.
    // Enough to tell apart records captured within one second, which is what
    // the suffix is for; the prefix carries the order.
    static thread_local std::mt19937 engine{std::random_device{}()};
    std::uniform_int_distribution<std::uint32_t> bytes{0, 0xFFFFFFU};
    const std::tm utc = utc_parts(now);
    std::ostringstream out;
    out << "kr-" << std::put_time(&utc, "%Y%m%dT%H%M%SZ") << "-" << std::hex << std::setw(6)
        << std::setfill('0') << bytes(engine);
    return out.str();
}

std::string timestamp_for(std::chrono::system_clock::time_point now) {
    const std::tm utc = utc_parts(now);
    const auto micros =
        std::chrono::duration_cast<std::chrono::microseconds>(now.time_since_epoch()) %
        std::chrono::seconds{1};
    std::ostringstream out;
    out << std::put_time(&utc, "%Y-%m-%dT%H:%M:%S") << "." << std::setw(6) << std::setfill('0')
        << micros.count() << "Z";
    return out.str();
}

void normalize(Record& record) {
    record.intent = trim(record.intent);
    record.decision = trim(record.decision);
    record.discipline = lower(trim(record.discipline));
    record.downstream_link = trim(record.downstream_link);
    record.status = normalize_status(record.status);
    record.provenance.source = trim(record.provenance.source);
    record.provenance.attribution = trim(record.provenance.attribution);
    record.supersedes = trim(record.supersedes);
    if (record.provenance.source.empty()) {
        record.provenance.source = std::string{kDefaultSource};
    }
}

std::string validate(const Record& record) {
    if (trim(record.intent).empty()) {
        return "the record has no intent -- the why is required";
    }
    if (!is_valid_status(record.status)) {
        std::string accepted;
        for (const std::string_view status : kStatuses) {
            accepted += accepted.empty() ? "" : ", ";
            accepted += status;
        }
        return "invalid status '" + record.status + "' (must be one of " + accepted + ")";
    }
    return {};
}

std::string index_text(const Record& record) {
    std::string out = record.intent;
    if (!record.decision.empty()) {
        out += "\n\nDecision: ";
        out += record.decision;
    }
    return out;
}

Record anonymize(Record record) {
    record.provenance.attribution.clear();
    return record;
}

void to_json(nlohmann::json& out, const Record& record) {
    out = nlohmann::json{{"id", record.id},
                         {"intent", record.intent},
                         {"decision", record.decision},
                         {"status", record.status},
                         {"discipline", record.discipline},
                         {"downstream_link", record.downstream_link},
                         {"provenance", nlohmann::json{{"source", record.provenance.source}}},
                         {"timestamp", record.timestamp}};
    if (!record.provenance.attribution.empty()) {
        out["provenance"]["attribution"] = record.provenance.attribution;
    }
    if (!record.raw_ref.empty()) {
        out["raw_ref"] = record.raw_ref;
    }
    if (!record.supersedes.empty()) {
        out["supersedes"] = record.supersedes;
    }
}

void from_json(const nlohmann::json& in, Record& record) {
    record = Record{};
    if (!in.is_object()) {
        return;
    }
    record.id = string_or_empty(in, "id");
    record.intent = string_or_empty(in, "intent");
    record.decision = string_or_empty(in, "decision");
    record.status = string_or_empty(in, "status");
    record.discipline = string_or_empty(in, "discipline");
    record.downstream_link = string_or_empty(in, "downstream_link");
    record.raw_ref = string_or_empty(in, "raw_ref");
    record.timestamp = string_or_empty(in, "timestamp");
    record.supersedes = string_or_empty(in, "supersedes");
    if (const auto provenance = in.find("provenance");
        provenance != in.end() && provenance->is_object()) {
        record.provenance.source = string_or_empty(*provenance, "source");
        record.provenance.attribution = string_or_empty(*provenance, "attribution");
    }
}

std::optional<Record> record_from_metadata(std::string_view source, std::string_view metadata,
                                           std::string& error) {
    if (trim(metadata).empty()) {
        error = "chunk '" + std::string{source} + "' has no record metadata";
        return std::nullopt;
    }
    const nlohmann::json parsed = nlohmann::json::parse(metadata, nullptr, false);
    if (parsed.is_discarded() || !parsed.is_object()) {
        error = "chunk '" + std::string{source} + "': the metadata is not a record";
        return std::nullopt;
    }
    Record record = parsed.get<Record>();
    if (record.id.empty() || record.id != source) {
        error =
            "chunk '" + std::string{source} + "': the metadata is not a record keyed by its own id";
        return std::nullopt;
    }
    if (trim(record.intent).empty()) {
        error = "record '" + std::string{source} + "' has no intent";
        return std::nullopt;
    }
    error.clear();
    return record;
}

}  // namespace apogee::knowledge
