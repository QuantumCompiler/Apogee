#pragma once

#include <sqlite3.h>

#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>

#include "embedstore/store.h"

/// Package-private: the connection and the statement helpers the store's
/// translation units share (`store.cpp`, `graph.cpp`, `graph_search.cpp`,
/// `graph_communities.cpp`, `graph_dedupe.cpp`).
///
/// The raw `sqlite3*` never leaves the package: it is wrapped in a
/// `unique_ptr` here, no public accessor returns it, and only members of
/// `Store` -- whichever file they are defined in -- can reach `impl_`. That
/// is the Code Style rule for C APIs applied to a class whose implementation
/// spans five files rather than one.
namespace apogee::embedstore::detail {

struct ConnectionDeleter {
    void operator()(sqlite3* handle) const noexcept {
        sqlite3_close(handle);
    }
};

using ConnectionPtr = std::unique_ptr<sqlite3, ConnectionDeleter>;

struct StatementDeleter {
    void operator()(sqlite3_stmt* statement) const noexcept {
        sqlite3_finalize(statement);
    }
};

using StatementPtr = std::unique_ptr<sqlite3_stmt, StatementDeleter>;

[[noreturn]] inline void fail(sqlite3* handle, std::string_view what) {
    throw std::runtime_error(std::string{what} + ": " + sqlite3_errmsg(handle));
}

inline void exec(sqlite3* handle, const char* sql) {
    char* message = nullptr;
    if (sqlite3_exec(handle, sql, nullptr, nullptr, &message) != SQLITE_OK) {
        // sqlite3_free, not delete: the message is SQLite's allocation.
        const std::string detail = message == nullptr ? "unknown error" : message;
        sqlite3_free(message);
        throw std::runtime_error("database error: " + detail);
    }
}

[[nodiscard]] inline StatementPtr prepare(sqlite3* handle, std::string_view sql) {
    sqlite3_stmt* raw = nullptr;
    if (sqlite3_prepare_v2(handle, sql.data(), static_cast<int>(sql.size()), &raw, nullptr) !=
        SQLITE_OK) {
        fail(handle, "could not prepare a statement");
    }
    return StatementPtr{raw};
}

inline void bind_text(sqlite3_stmt* statement, int index, std::string_view value) {
    // SQLITE_TRANSIENT: SQLite copies, so the caller's buffer need not outlive
    // the step. The alternative is a dangling read that works until it does not.
    sqlite3_bind_text(statement, index, value.data(), static_cast<int>(value.size()),
                      SQLITE_TRANSIENT);
}

[[nodiscard]] inline std::string column_text(sqlite3_stmt* statement, int index) {
    const auto* bytes = sqlite3_column_text(statement, index);
    if (bytes == nullptr) {
        return {};
    }
    // SQLite hands back unsigned char*; the same bytes as char* is what a
    // std::string holds. There is no way to cross that without a cast.
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
    return std::string{reinterpret_cast<const char*>(bytes),
                       static_cast<std::size_t>(sqlite3_column_bytes(statement, index))};
}

/// Whether `table` already has `column` -- the guard a schema migration needs,
/// since SQLite's ADD COLUMN has no IF NOT EXISTS.
[[nodiscard]] inline bool has_column(sqlite3* handle, std::string_view table,
                                     std::string_view column) {
    StatementPtr info = prepare(handle, "PRAGMA table_info(" + std::string{table} + ")");
    while (sqlite3_step(info.get()) == SQLITE_ROW) {
        if (column_text(info.get(), 1) == column) {
            return true;
        }
    }
    return false;
}

/// Runs `body` inside a transaction, rolling back if it throws.
template <typename Body>
void in_transaction(sqlite3* handle, Body&& body) {
    exec(handle, "BEGIN IMMEDIATE");
    try {
        body();
    } catch (...) {
        exec(handle, "ROLLBACK");
        throw;
    }
    exec(handle, "COMMIT");
}

/// `?,?,...` for `count` bound parameters.
[[nodiscard]] inline std::string placeholders(std::size_t count) {
    std::string out;
    for (std::size_t i = 0; i < count; ++i) {
        out += i == 0 ? "?" : ",?";
    }
    return out;
}

/// The knowledge-graph tables, the entity full-text index and its triggers
/// (schema v4; the community tables since v5). Called from the store's
/// constructor inside the one migration transaction; idempotent, with
/// dropped triggers self-healing. Defined in `graph.cpp`, beside the code
/// that writes those tables.
void ensure_graph_schema(sqlite3* handle);

}  // namespace apogee::embedstore::detail

namespace apogee::embedstore {

struct Store::Impl {
    detail::ConnectionPtr connection;
};

}  // namespace apogee::embedstore
