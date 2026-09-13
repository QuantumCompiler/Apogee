#include "httpserver/admin_auth.h"

#include <nlohmann/json.hpp>

#include <cctype>
#include <fstream>
#include <iomanip>
#include <random>
#include <sstream>
#include <stdexcept>

#include "harness/config_edit.h"

namespace apogee::httpserver {
namespace {

constexpr std::string_view kAuthenticationError = "authentication_error";

std::string trim(std::string_view text) {
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.front())) != 0) {
        text.remove_prefix(1);
    }
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.back())) != 0) {
        text.remove_suffix(1);
    }
    return std::string{text};
}

}  // namespace

std::filesystem::path admin_token_path(const std::filesystem::path& config_path) {
    return config_path.parent_path() / std::string{kAdminTokenFileName};
}

std::string generate_admin_token() {
    std::random_device device;
    std::ostringstream out;
    out << std::hex << std::setfill('0');
    for (int i = 0; i < 8; ++i) {
        out << std::setw(8) << device();
    }
    return out.str();
}

std::string load_or_create_admin_token(const std::filesystem::path& config_path) {
    const std::filesystem::path path = admin_token_path(config_path);
    if (std::filesystem::exists(path)) {
        std::ifstream in{path};
        if (!in) {
            throw std::runtime_error("could not read the admin token at " + path.string());
        }
        std::string token;
        std::getline(in, token);
        token = trim(token);
        if (!token.empty()) {
            return token;
        }
        // An empty file is a broken one; regenerate rather than serve a plane
        // nobody can enter.
    }
    const std::string token = generate_admin_token();
    try {
        std::filesystem::create_directories(path.parent_path());
        // Written 0600 before it is renamed into place: a secret is never on
        // disk under a permissive mode, not even between two syscalls.
        harness::write_file_atomically(path, token + "\n", /*private_mode=*/true);
    } catch (const std::exception& e) {
        throw std::runtime_error("could not write the admin token at " + path.string() + ": " +
                                 e.what());
    }
    return token;
}

bool constant_time_equal(std::string_view lhs, std::string_view rhs) noexcept {
    if (lhs.size() != rhs.size()) {
        return false;
    }
    unsigned char difference = 0;
    for (std::size_t i = 0; i < lhs.size(); ++i) {
        difference = static_cast<unsigned char>(
            difference | static_cast<unsigned char>(static_cast<unsigned char>(lhs[i]) ^
                                                    static_cast<unsigned char>(rhs[i])));
    }
    return difference == 0;
}

std::string bearer_of(const HttpRequest& request) {
    const std::string header = request.header("authorization");
    constexpr std::string_view kScheme = "Bearer ";
    if (header.size() <= kScheme.size()) {
        return {};
    }
    // The scheme is case-insensitive; the token is not.
    for (std::size_t i = 0; i < kScheme.size(); ++i) {
        if (std::tolower(static_cast<unsigned char>(header[i])) !=
            std::tolower(static_cast<unsigned char>(kScheme[i]))) {
            return {};
        }
    }
    return trim(std::string_view{header}.substr(kScheme.size()));
}

bool bearer_valid(const HttpRequest& request, std::string_view token) {
    if (token.empty()) {
        return false;
    }
    return constant_time_equal(bearer_of(request), token);
}

HttpResponse unauthorized_response() {
    HttpResponse response = error_response(
        401,
        "admin authorization required -- send Authorization: Bearer <token> (see 'apogee serve "
        "--print-admin-token')",
        kAuthenticationError);
    response.headers["WWW-Authenticate"] = "Bearer realm=\"apogee-admin\"";
    return response;
}

}  // namespace apogee::httpserver
