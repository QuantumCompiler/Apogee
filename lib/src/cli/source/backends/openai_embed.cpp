#include "backends/openai_embed.h"

#include <stdexcept>

namespace apogee::backends::openai_embed {

std::string_view default_model() noexcept {
    return "text-embedding-3-small";
}

std::size_t known_dimensions(std::string_view model) noexcept {
    // The documented native widths. `text-embedding-3-*` accept a smaller
    // `dimensions` on request; Apogee does not ask for one, so the native
    // width is what comes back.
    if (model == "text-embedding-3-small" || model == "text-embedding-ada-002") {
        return 1536;
    }
    if (model == "text-embedding-3-large") {
        return 3072;
    }
    return 0;
}

nlohmann::json build_request(std::string_view model, const std::vector<std::string>& inputs) {
    // `encoding_format: float` is the documented default, stated anyway: the
    // alternative is base64, and a parser written for one silently reads
    // garbage from the other.
    return nlohmann::json{
        {"model", std::string{model}}, {"input", inputs}, {"encoding_format", "float"}};
}

std::vector<std::vector<float>> parse_response(const nlohmann::json& body, std::size_t expected) {
    const auto data = body.find("data");
    if (data == body.end() || !data->is_array()) {
        throw std::runtime_error("embeddings response has no 'data' array");
    }
    if (data->size() != expected) {
        throw std::runtime_error("embeddings response carried " + std::to_string(data->size()) +
                                 " vector(s) for " + std::to_string(expected) + " input(s)");
    }

    std::vector<std::vector<float>> vectors(expected);
    std::vector<bool> filled(expected, false);
    for (const nlohmann::json& row : *data) {
        const auto index = row.find("index");
        const auto embedding = row.find("embedding");
        if (index == row.end() || !index->is_number_unsigned() || embedding == row.end() ||
            !embedding->is_array()) {
            throw std::runtime_error("an embeddings row is missing its index or its vector");
        }
        const auto at = index->get<std::size_t>();
        if (at >= expected || filled[at]) {
            throw std::runtime_error("an embeddings row has an index outside the request");
        }
        vectors[at] = embedding->get<std::vector<float>>();
        filled[at] = true;
    }
    return vectors;
}

}  // namespace apogee::backends::openai_embed
