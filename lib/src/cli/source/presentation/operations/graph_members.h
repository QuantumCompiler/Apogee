#pragma once

#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "contracts/config.h"
#include "embedstore/graph.h"
#include "embedstore/store.h"
#include "graph/build.h"

/// A named graph's members, opened the same way by the command line and the
/// admin twins, and the rules a `graphs:` entry must satisfy (A4: moved out
/// of `cli/graph`).
namespace apogee::commands {

/// A named graph's member collections, opened read-only for a build, stats
/// or show; a member whose database is missing maps to null and contributes
/// nothing (`apogee check` reports the config-level problem). Shared by the
/// CLI and the admin twins so a member is opened the same way on both.
struct GraphMembers {
    std::map<std::string, std::unique_ptr<embedstore::Store>> stores;

    /// The non-owning views the store's multi-member calls take.
    [[nodiscard]] embedstore::MemberStores views() const;
    /// In the entry's order -- the order the build plans in.
    [[nodiscard]] std::vector<graph::Member> members(const harness::NamedGraphConfig& named) const;
    /// Whether any member holds a chunk at all.
    [[nodiscard]] bool any_data() const;
};

[[nodiscard]] GraphMembers open_graph_members(const harness::NamedGraphConfig& named);

/// The write-time rules a `graphs:` entry must satisfy, decided ONCE for
/// `config add-graph` and its admin twin: a name, at least one member, no
/// collision with a collection name (the resolution rule depends on it),
/// a configured `extract_backend` when one is named, sane knobs. Unknown
/// members are warnings, not errors -- ingest registers collections on
/// first use. A non-empty `error` means the entry is refused.
struct NamedGraphValidation {
    std::string error;
    std::vector<std::string> warnings;
};

[[nodiscard]] NamedGraphValidation validate_named_graph(const harness::Config& config,
                                                        std::string_view name,
                                                        const harness::NamedGraphConfig& graph);

}  // namespace apogee::commands
