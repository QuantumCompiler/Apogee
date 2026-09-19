#include "embedstore/graph_communities.h"

#include <chrono>
#include <ctime>
#include <string>
#include <utility>
#include <vector>

#include "embedstore/store.h"
#include "embedstore/store_impl.h"

namespace apogee::embedstore {

using detail::bind_text;
using detail::column_text;
using detail::fail;
using detail::in_transaction;
using detail::prepare;
using detail::StatementPtr;

namespace {

constexpr std::string_view kNodeColumns =
    "n.id, n.name, n.name_norm, n.type, n.description, n.dim, n.mention_count,"
    " COALESCE(n.metadata, '')";

[[nodiscard]] GraphNode node_row(sqlite3_stmt* statement) {
    GraphNode node;
    node.id = sqlite3_column_int64(statement, 0);
    node.name = column_text(statement, 1);
    node.name_norm = column_text(statement, 2);
    node.type = column_text(statement, 3);
    node.description = column_text(statement, 4);
    node.dim = sqlite3_column_int64(statement, 5);
    node.mention_count = sqlite3_column_int64(statement, 6);
    node.metadata = column_text(statement, 7);
    return node;
}

[[nodiscard]] std::string now_rfc3339() {
    const std::time_t seconds =
        std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    std::tm utc{};
#ifdef _WIN32
    gmtime_s(&utc, &seconds);
#else
    gmtime_r(&seconds, &utc);
#endif
    char buffer[32];
    if (std::strftime(buffer, sizeof buffer, "%Y-%m-%dT%H:%M:%SZ", &utc) == 0) {
        return {};
    }
    return buffer;
}

/// Removes one community's row, membership and pseudo-chunk. Inside the
/// caller's transaction.
void remove_community(sqlite3* handle, std::int64_t id) {
    for (const char* sql : {"DELETE FROM kg_community_members WHERE community_id = ?",
                            "DELETE FROM kg_communities WHERE id = ?"}) {
        StatementPtr remove = prepare(handle, sql);
        sqlite3_bind_int64(remove.get(), 1, id);
        if (sqlite3_step(remove.get()) != SQLITE_DONE) {
            fail(handle, "could not remove a community");
        }
    }
    StatementPtr chunk = prepare(handle, "DELETE FROM chunks WHERE source = ?");
    bind_text(chunk.get(), 1, community_source(id));
    if (sqlite3_step(chunk.get()) != SQLITE_DONE) {
        fail(handle, "could not remove a community's summary chunk");
    }
}

}  // namespace

std::string community_source(std::int64_t id) {
    return std::string{kCommunitySourcePrefix} + std::to_string(id);
}

bool is_community_source(std::string_view source) noexcept {
    return source.starts_with(kCommunitySourcePrefix);
}

std::vector<GraphCommunity> Store::graph_communities() const {
    StatementPtr select = prepare(impl_->connection.get(),
                                  "SELECT id, member_key, size, summary, model, summarized_at"
                                  " FROM kg_communities ORDER BY size DESC, id");
    std::vector<GraphCommunity> out;
    while (sqlite3_step(select.get()) == SQLITE_ROW) {
        GraphCommunity community;
        community.id = sqlite3_column_int64(select.get(), 0);
        community.member_key = column_text(select.get(), 1);
        community.size = sqlite3_column_int64(select.get(), 2);
        community.summary = column_text(select.get(), 3);
        community.model = column_text(select.get(), 4);
        community.summarized_at = column_text(select.get(), 5);
        out.push_back(std::move(community));
    }
    return out;
}

std::vector<GraphNode> Store::community_members(std::int64_t community_id) const {
    StatementPtr select =
        prepare(impl_->connection.get(), "SELECT " + std::string{kNodeColumns} +
                                             "  FROM kg_community_members m"
                                             "  JOIN kg_nodes n ON n.id = m.node_id"
                                             " WHERE m.community_id = ?"
                                             " ORDER BY n.mention_count DESC, n.name");
    sqlite3_bind_int64(select.get(), 1, community_id);
    std::vector<GraphNode> out;
    while (sqlite3_step(select.get()) == SQLITE_ROW) {
        out.push_back(node_row(select.get()));
    }
    return out;
}

std::int64_t Store::replace_community(std::string_view member_key,
                                      const std::vector<std::int64_t>& members,
                                      std::string_view summary, std::string_view model) {
    sqlite3* handle = impl_->connection.get();
    std::int64_t id = 0;
    in_transaction(handle, [&] {
        // A forced regeneration of the same membership replaces the row and
        // its pseudo-chunk rather than leaving two summaries of one cluster.
        StatementPtr lookup = prepare(handle, "SELECT id FROM kg_communities WHERE member_key = ?");
        bind_text(lookup.get(), 1, member_key);
        if (sqlite3_step(lookup.get()) == SQLITE_ROW) {
            remove_community(handle, sqlite3_column_int64(lookup.get(), 0));
        }
        StatementPtr insert = prepare(handle,
                                      "INSERT INTO kg_communities (member_key, size, summary,"
                                      " model, summarized_at) VALUES (?, ?, ?, ?, ?)");
        bind_text(insert.get(), 1, member_key);
        sqlite3_bind_int64(insert.get(), 2, static_cast<sqlite3_int64>(members.size()));
        bind_text(insert.get(), 3, summary);
        bind_text(insert.get(), 4, model);
        bind_text(insert.get(), 5, now_rfc3339());
        if (sqlite3_step(insert.get()) != SQLITE_DONE) {
            fail(handle, "could not store a community");
        }
        id = sqlite3_last_insert_rowid(handle);
        StatementPtr member = prepare(
            handle, "INSERT INTO kg_community_members (community_id, node_id) VALUES (?, ?)");
        for (const std::int64_t node_id : members) {
            sqlite3_reset(member.get());
            sqlite3_bind_int64(member.get(), 1, id);
            sqlite3_bind_int64(member.get(), 2, node_id);
            if (sqlite3_step(member.get()) != SQLITE_DONE) {
                fail(handle, "could not store a community's membership");
            }
        }
        // The lexical pseudo-chunk: searchable at once through the chunk
        // index's insert trigger, vectorised later.
        StatementPtr chunk = prepare(handle,
                                     "INSERT INTO chunks (source, ordinal, text, embedding, dim,"
                                     " metadata) VALUES (?, 0, ?, NULL, 0, NULL)");
        bind_text(chunk.get(), 1, community_source(id));
        bind_text(chunk.get(), 2, summary);
        if (sqlite3_step(chunk.get()) != SQLITE_DONE) {
            fail(handle, "could not store a community's summary chunk");
        }
    });
    return id;
}

void Store::update_community_embedding(std::int64_t community_id,
                                       const std::vector<float>& vector) {
    StatementPtr select =
        prepare(impl_->connection.get(), "SELECT summary FROM kg_communities WHERE id = ?");
    sqlite3_bind_int64(select.get(), 1, community_id);
    if (sqlite3_step(select.get()) != SQLITE_ROW) {
        throw std::runtime_error("no community with id " + std::to_string(community_id));
    }
    const std::string summary = column_text(select.get(), 0);
    replace_source(community_source(community_id), {summary}, {vector});
}

std::int64_t Store::prune_communities(const std::set<std::string>& keep) {
    sqlite3* handle = impl_->connection.get();
    std::int64_t pruned = 0;
    const std::vector<GraphCommunity> all = graph_communities();
    in_transaction(handle, [&] {
        for (const GraphCommunity& community : all) {
            if (keep.contains(community.member_key)) {
                continue;
            }
            remove_community(handle, community.id);
            ++pruned;
        }
    });
    return pruned;
}

std::vector<std::int64_t> Store::communities_without_vectors() const {
    StatementPtr select = prepare(impl_->connection.get(),
                                  "SELECT c.id FROM kg_communities c"
                                  " WHERE NOT EXISTS (SELECT 1 FROM chunks"
                                  "   WHERE chunks.source = ? || c.id AND chunks.dim > 0)"
                                  " ORDER BY c.id");
    bind_text(select.get(), 1, kCommunitySourcePrefix);
    std::vector<std::int64_t> out;
    while (sqlite3_step(select.get()) == SQLITE_ROW) {
        out.push_back(sqlite3_column_int64(select.get(), 0));
    }
    return out;
}

}  // namespace apogee::embedstore
