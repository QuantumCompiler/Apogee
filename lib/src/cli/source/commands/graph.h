#pragma once

#include <string_view>

#include "commands/command.h"

/// `apogee graph` -- build and inspect the knowledge graph over a collection.
///
/// `build` runs the extraction clerk once per stale chunk -- one structured
/// call each, validated in host code -- into `kg_*` tables inside the
/// collection's own database, incrementally and resumably; `stats`, `show`
/// and `delete` inspect and clear it. Retrieval-time expansion consumes what
/// build produces, on every RAG surface, once the collection's `graph.enabled`
/// is set -- which the first successful build does through the one config
/// editor.
///
/// **Local extractor by default.** A full build never runs on a metered
/// backend on Apogee's initiative: it extracts on one only when that backend
/// was named explicitly (`-m`, the collection's `graph.extract_backend`, or
/// the extraction role). Whether a backend is metered is a fact the provider
/// states, discovered through the Harness.
namespace apogee::commands {

class GraphCommand final : public Command {
public:
    [[nodiscard]] std::string_view name() const noexcept override;
    [[nodiscard]] std::string_view summary() const noexcept override;
    void bind(CLI::App& root, const RootContext& context) override;
};

}  // namespace apogee::commands
