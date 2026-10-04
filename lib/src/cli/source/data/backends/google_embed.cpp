#include "backends/google_embed.h"

#include <stdexcept>

namespace apogee::backends::google_embed {

std::string_view default_model() noexcept {
    return "gemini-embedding-001";
}

std::size_t known_dimensions(std::string_view model) noexcept {
    // The documented native width. The model accepts a smaller
    // `outputDimensionality`; Apogee does not ask for one.
    if (model == "gemini-embedding-001") {
        return 3072;
    }
    return 0;
}

nlohmann::json build_request(std::string_view model, const std::vector<std::string>& inputs) {
    nlohmann::json requests = nlohmann::json::array();
    for (const std::string& input : inputs) {
        requests.push_back(nlohmann::json{
            // Repeated per row, with the `models/` prefix the API requires
            // there and not in the URL. Omitting it fails the whole batch.
            {"model", "models/" + std::string{model}},
            {"content", nlohmann::json{{"parts", nlohmann::json::array({{{"text", input}}})}}},
        });
    }
    return nlohmann::json{{"requests", std::move(requests)}};
}

std::vector<std::vector<float>> parse_response(const nlohmann::json& body, std::size_t expected) {
    const auto embeddings = body.find("embeddings");
    if (embeddings == body.end() || !embeddings->is_array()) {
        throw std::runtime_error("embeddings response has no 'embeddings' array");
    }
    if (embeddings->size() != expected) {
        throw std::runtime_error("embeddings response carried " +
                                 std::to_string(embeddings->size()) + " vector(s) for " +
                                 std::to_string(expected) + " input(s)");
    }

    std::vector<std::vector<float>> vectors;
    vectors.reserve(expected);
    for (const nlohmann::json& row : *embeddings) {
        const auto values = row.find("values");
        if (values == row.end() || !values->is_array()) {
            throw std::runtime_error("an embeddings row has no 'values' vector");
        }
        vectors.push_back(values->get<std::vector<float>>());
    }
    return vectors;
}

}  // namespace apogee::backends::google_embed
