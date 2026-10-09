#pragma once

#include <nlohmann/json_fwd.hpp>

#include <string>
#include <string_view>

/// The bridge between the config's two formats (28i): the legacy YAML the
/// compat read still loads, and the JSONC every new config is written in.
///
/// The line editor's transforms (`config_edit.h`) are defined once, over
/// YAML text. A JSONC config is edited through them: its value written as
/// plain block YAML (`yaml_of_json`), the transform applied, the result read
/// back (`json_of_yaml`) and spliced into the JSONC text in place
/// (`jsonc::patch`) -- so each edit means exactly one thing in either format,
/// and the JSONC file keeps every comment and byte the edit does not touch.
/// `config migrate` reads a YAML file's values through the same typing.
namespace apogee::harness {

/// `value` -- an object -- as the block YAML the line editor reads: two-space
/// indentation, a section or entry with nothing under it as a bare `key:`, an
/// empty list as `[]`, a list of scalars as a one-line flow list, every
/// string double-quoted, keys plain where YAML reads them back unchanged.
[[nodiscard]] std::string yaml_of_json(const nlohmann::ordered_json& value);

/// YAML text read back as a value. A quoted or block scalar is a string; a
/// plain one is null, a boolean or a number when YAML's core schema reads it
/// so (`null`, `~`, `true`, `False`, a JSON-shaped number), a string
/// otherwise. `hint` is the value the text was written from: where YAML
/// spells nothing (`key:`) and the hint held an object or list, it stays one
/// -- empty, if the edit took its last member -- so an untouched `{}` is not
/// rewritten as `null`, nor an emptied section's comments with it. Throws
/// ConfigError when the text is not YAML.
[[nodiscard]] nlohmann::ordered_json json_of_yaml(std::string_view yaml,
                                                  const nlohmann::ordered_json& hint);

/// One scalar's value under that typing: `plain` false (quoted, a block) is
/// always a string.
[[nodiscard]] nlohmann::ordered_json json_of_yaml_scalar(std::string_view scalar, bool plain);

}  // namespace apogee::harness
