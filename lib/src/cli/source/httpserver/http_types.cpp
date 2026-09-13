#include "httpserver/http_types.h"

#include <nlohmann/json.hpp>

#include <cctype>

namespace apogee::httpserver {
namespace {

std::string lower(std::string_view text) {
    std::string out{text};
    for (char& c : out) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return out;
}

}  // namespace

bool HttpRequest::has_query(std::string_view key) const {
    return query.contains(std::string{key});
}

std::string HttpRequest::query_value(std::string_view key) const {
    const auto it = query.find(std::string{key});
    return it == query.end() ? std::string{} : it->second;
}

std::string HttpRequest::header(std::string_view name) const {
    const auto it = headers.find(lower(name));
    return it == headers.end() ? std::string{} : it->second;
}

HttpResponse json_response(int status, const nlohmann::json& body) {
    HttpResponse response;
    response.status = status;
    response.content_type = "application/json";
    response.body = body.dump();
    return response;
}

nlohmann::json error_body(std::string_view message, std::string_view type) {
    return nlohmann::json{
        {"error", {{"message", std::string{message}}, {"type", std::string{type}}}}};
}

HttpResponse error_response(int status, std::string_view message, std::string_view type) {
    return json_response(status, error_body(message, type));
}

}  // namespace apogee::httpserver
