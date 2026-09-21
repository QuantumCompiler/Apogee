#pragma once

#include <filesystem>
#include <string>
#include <string_view>

#include "httpserver/http_types.h"

/// Bearer authentication for the control plane.
///
/// The token is a per-install secret: 32 random bytes as hex, generated lazily
/// the first time a server needs it, written `0600` as a sibling of
/// `config.yaml` -- so it follows `--config` and stays hermetic in tests -- and
/// read back with `apogee serve --print-admin-token`. It is accepted
/// **header-only**: `Authorization: Bearer <token>`. A query-string token is
/// deliberately refused even when correct, because a query string lands in
/// request logs and a header does not. The compare is constant-time.
///
/// Nothing here serializes the token. It is the first instance of the rule
/// that a secret is unleakable by construction: there is no type carrying it
/// that anything turns into JSON.
namespace apogee::httpserver {

inline constexpr std::string_view kAdminTokenFileName = "admin-token";

/// `<dir of config_path>/admin-token`.
[[nodiscard]] std::filesystem::path admin_token_path(const std::filesystem::path& config_path);

/// The token, generated and written `0600` when the file does not exist yet.
/// Throws std::runtime_error when it cannot be read or written.
[[nodiscard]] std::string load_or_create_admin_token(const std::filesystem::path& config_path);

/// A fresh token: 64 hex characters from the platform's random device.
[[nodiscard]] std::string generate_admin_token();

/// Equality that takes the same time whatever the first differing byte.
[[nodiscard]] bool constant_time_equal(std::string_view lhs, std::string_view rhs) noexcept;

/// The bearer a request carries in its `Authorization` header, or empty when
/// the header is absent or not a `Bearer` scheme. Never reads the query.
[[nodiscard]] std::string bearer_of(const HttpRequest& request);

/// Whether `request` carries exactly `token`. False for an empty token: a
/// plane with no token is a plane nobody can enter.
[[nodiscard]] bool bearer_valid(const HttpRequest& request, std::string_view token);

/// `401` with `WWW-Authenticate: Bearer`, in the error envelope.
[[nodiscard]] HttpResponse unauthorized_response();

}  // namespace apogee::httpserver
