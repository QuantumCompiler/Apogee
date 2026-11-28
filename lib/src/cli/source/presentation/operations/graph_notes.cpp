#include "operations/graph_notes.h"

namespace apogee::commands {

std::string entity_vectors_skipped(std::string_view graph, std::string_view why) {
    return "entity vectors skipped for \"" + std::string{graph} + "\" -- " + std::string{why} +
           "; entity search will be full-text only.";
}

std::string summary_vectors_skipped(std::string_view graph, std::string_view why) {
    return "summary vectors skipped for \"" + std::string{graph} + "\" -- " + std::string{why} +
           "; summaries will be full-text searchable only.";
}

std::string member_without_database(std::string_view collection) {
    return "member collection \"" + std::string{collection} +
           "\" has no database -- it contributes nothing this build.";
}

std::string only_code_built(std::string_view graph) {
    return "no member collection of \"" + std::string{graph} +
           "\" has any data yet -- only its code was built.";
}

std::vector<std::string> members_without_database(const GraphMembers& members) {
    std::vector<std::string> out;
    for (const auto& [collection, store] : members.stores) {
        if (store == nullptr) {
            out.push_back(member_without_database(collection));
        }
    }
    return out;
}

}  // namespace apogee::commands
