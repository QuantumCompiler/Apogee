#pragma once

#include <filesystem>
#include <string_view>

#include "httpserver/http_types.h"
#include "secrets/resolve.h"

/// The credential slice of the control plane: `/v1/admin/auth`.
///
/// Three routes over the store, as free functions over a config path so the
/// tests call exactly what the routes call. `GET` lists metadata -- the type
/// with no key field -- plus which source answers for each configured cloud
/// backend. `PUT` takes a key **in the body only** and is served to
/// **loopback peers only**, judged from the socket's own address and never a
/// header a client can set: `403` from anywhere else, whatever the bind.
/// `DELETE` clears; it accepts no secret and mints none, so the bearer alone
/// gates it.
namespace apogee::httpserver {

struct AdminAuthContext {
    std::filesystem::path config_path;
    /// The environment the listing reports against. Null means the
    /// process-wide snapshot.
    const secrets::EnvSnapshot* env = nullptr;
};

/// `GET /v1/admin/auth`.
[[nodiscard]] HttpResponse admin_list_credentials(const AdminAuthContext& context);

/// `PUT /v1/admin/auth/{id}` -- the `auth add` twin. Body: `{"key": "…"}`.
[[nodiscard]] HttpResponse admin_put_credential(const AdminAuthContext& context,
                                                std::string_view provider,
                                                const HttpRequest& request);

/// `DELETE /v1/admin/auth/{id}` -- the `auth clear` twin.
[[nodiscard]] HttpResponse admin_clear_credential(const AdminAuthContext& context,
                                                  std::string_view provider);

}  // namespace apogee::httpserver
