#include "knowledge/export.h"

#include <map>
#include <utility>

namespace apogee::knowledge {
namespace {

void write_field(std::string& out, std::string_view label, const std::string& value) {
    if (value.empty()) {
        return;
    }
    out += "- **";
    out += label;
    out += ":** ";
    out += value;
    out += "\n";
}

void write_record(std::string& out, const Record& record) {
    out += "### " + record.id + "\n\n";
    write_field(out, "Intent", record.intent);
    write_field(out, "Decision", record.decision);
    write_field(out, "Status", record.status);
    write_field(out, "Discipline", record.discipline);
    write_field(out, "Link", record.downstream_link);
    write_field(out, "Source", record.provenance.source);
    write_field(out, "Attribution", record.provenance.attribution);
    write_field(out, "Supersedes", record.supersedes);
    write_field(out, "Captured", record.timestamp);
    out += "\n";
}

}  // namespace

std::vector<Record> export_records(std::vector<Record> records, bool strip_names) {
    if (!strip_names) {
        return records;
    }
    for (Record& record : records) {
        record = anonymize(record);
        record.raw_ref.clear();
    }
    return records;
}

std::string render_markdown(const std::vector<Record>& records) {
    std::string out = "# Knowledge records\n\n";
    out += std::to_string(records.size()) + " record(s).\n";

    std::map<std::string, std::vector<Record>> grouped;
    std::vector<Record> others;
    for (const Record& record : records) {
        if (is_valid_status(record.status)) {
            grouped[record.status].push_back(record);
        } else {
            others.push_back(record);
        }
    }
    const auto emit = [&out](std::string_view title, const std::vector<Record>& group) {
        if (group.empty()) {
            return;
        }
        out += "\n## " + std::string{title} + " (" + std::to_string(group.size()) + ")\n\n";
        for (const Record& record : group) {
            write_record(out, record);
        }
    };
    for (const std::string_view status : valid_statuses()) {
        emit(status, grouped[std::string{status}]);
    }
    emit("other", others);
    return out;
}

}  // namespace apogee::knowledge
