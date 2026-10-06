#include "symphony/view.h"

#include <nlohmann/json.hpp>

#include <cmath>
#include <string>

namespace apogee::symphony {

nlohmann::json definition_document(const Definition& definition) {
    const harness::SymphonySpec& spec = definition.spec;
    nlohmann::json stages = nlohmann::json::array();
    for (const harness::SymphonyStage& stage : spec.stages) {
        nlohmann::json view{{"name", stage.name},
                            {"role", stage.role},
                            {"prompt", stage.prompt},
                            {"image", stage.image}};
        if (!stage.schema.empty()) {
            view["schema"] = stage.schema;
        }
        if (stage.brief_tokens.has_value()) {
            view["brief_tokens"] = *stage.brief_tokens;
        }
        if (stage.answer_tokens.has_value()) {
            view["answer_tokens"] = *stage.answer_tokens;
        }
        stages.push_back(std::move(view));
    }
    nlohmann::json out{
        {"object", "symphony"},
        {"name", spec.name},
        {"description", spec.description},
        {"source", std::string{to_string(definition.source)}},
        {"overrides", definition.overrides},
        {"input", {{"description", spec.input.description}, {"image", spec.input.image}}},
        {"stages", std::move(stages)},
        {"problems", validate(spec)},
    };
    if (!definition.path.empty()) {
        // Generic form on the wire, as the agents' paths are.
        out["path"] = definition.path.generic_string();
    }
    return out;
}

nlohmann::json list_document(const Catalog& catalog) {
    nlohmann::json data = nlohmann::json::array();
    for (const Definition& definition : catalog.definitions) {
        data.push_back(definition_document(definition));
    }
    return nlohmann::json{
        {"object", "list"}, {"data", std::move(data)}, {"problems", catalog.problems}};
}

nlohmann::json play_document(std::string_view symphony, std::string_view suite,
                             const PlayResult& result) {
    nlohmann::json stages = nlohmann::json::array();
    for (const StageResult& stage : result.stages) {
        nlohmann::json view{{"name", stage.name},
                            {"role", stage.role},
                            {"backend", stage.backend},
                            {"answer", stage.answer},
                            {"cut", stage.cut},
                            // Tenths: a timing, not a measurement to the bit.
                            {"seconds", std::round(stage.seconds * 10.0) / 10.0}};
        if (stage.tokens.has_value()) {
            view["tokens"] = *stage.tokens;
        }
        stages.push_back(std::move(view));
    }
    return nlohmann::json{
        {"object", "symphony.play"},
        {"symphony", std::string{symphony}},
        {"suite", suite.empty() ? nlohmann::json() : nlohmann::json(std::string{suite})},
        {"output", result.output},
        {"stages", std::move(stages)}};
}

}  // namespace apogee::symphony
