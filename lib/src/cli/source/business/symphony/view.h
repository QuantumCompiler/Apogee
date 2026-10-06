#pragma once

#include <nlohmann/json_fwd.hpp>

#include <string_view>
#include <vector>

#include "symphony/definition.h"
#include "symphony/runner.h"

/// The one view of a symphony every surface renders (27q): `symphonies
/// list|show --output-format json` print these documents and the admin
/// plane's `GET /v1/admin/symphonies[/{id}]` serves them, byte for byte --
/// the read convention 27j set. `symphonies play --output-format json`
/// prints its own.
namespace apogee::symphony {

/// One definition: `{"object": "symphony", "name", "description", "source",
/// "path"?, "overrides", "input": {"description", "image"}, "stages": [{"name",
/// "role", "prompt", "schema"?, "image", "brief_tokens"?, "answer_tokens"?}],
/// "problems": [...]}` -- a schema as its text, exactly as written; `path`
/// only for a definition read from a file; `problems` empty for a sound one.
[[nodiscard]] nlohmann::json definition_document(const Definition& definition);

/// Every definition: `{"object": "list", "data": [definition_document...],
/// "problems": [...]}` -- `problems` the spec files that could not be read.
[[nodiscard]] nlohmann::json list_document(const Catalog& catalog);

/// A finished play: `{"object": "symphony.play", "symphony", "suite",
/// "output", "stages": [{"name", "role", "backend", "answer", "cut",
/// "tokens"?, "seconds"}]}` -- `suite` the active one's name, null for none.
[[nodiscard]] nlohmann::json play_document(std::string_view symphony, std::string_view suite,
                                           const PlayResult& result);

}  // namespace apogee::symphony
