#pragma once

#include <nlohmann/json_fwd.hpp>

#include <span>
#include <string_view>

/// Machine mode's vocabulary, declared once as data (28g).
///
/// Every line type the protocol has -- each event the child writes, each line
/// a driver may send -- with its fields: the JSON type, whether it is always
/// there, what it means. Three views read this one declaration, so none can
/// drift from another:
///
///   - `capabilities` on the `session` event announces the outbound types
///     and the inbound ones a session reads (28d);
///   - `apogee __machine-schema` prints it as a JSON Schema (draft 2020-12)
///     a host validates a stream against or generates its types from -- the
///     binary prints the schema of exactly the build that runs it, and the
///     release archives carry the same bytes as `machine-schema.json`;
///   - `cli.machine_schema_conformance` holds it to what `json_reporter.cpp`
///     emits and parses and to what `machine-mode.md` documents, and the
///     unit suite holds every emitter's keys to it.
///
/// The schema states the stability promise structurally: an unknown `type`
/// validates (only a known type is held to its definition), every event
/// admits fields it does not name, and a value a field may grow (a
/// `finish_reason`, a `kind`) is a string, never a closed enum.
namespace apogee::commands {

/// Bumped only when an existing event's meaning changes.
///
/// Adding a new event type does **not** bump it: drivers are required to
/// ignore unknown types, so an addition is compatible by construction. The
/// version exists for the case that is not — a field changing meaning under a
/// name a driver already reads.
inline constexpr int kMachineProtocolVersion = 1;

/// The vocabulary's version (28d, 28g): a date, moved when the vocabulary a
/// release ships grows -- a new event type, a new field, a new inbound type.
/// `capabilities.schema` and the schema artifact carry it, so a host can
/// tell two builds' streams apart without diffing them. Unlike
/// `protocol_version` it says nothing about compatibility: every growth is
/// additive under the stability promise.
inline constexpr std::string_view kMachineSchemaVersion = "2026-10-07";

/// One field of a line.
struct FieldSpec {
    std::string_view name;
    /// Its JSON type: `string`, `integer`, `boolean`, `object`, `array`, or
    /// two of them joined by `|` (`object|null`).
    std::string_view type;
    /// Always present. A field that may be absent is optional -- and absent
    /// is not zero (`usage` when a provider reported none).
    bool required = false;
    std::string_view description;
    /// An object's own fields, or the fields of an array's object items.
    std::span<const FieldSpec> properties = {};
    /// An array's item type when its items are not objects (`string`).
    std::string_view items = {};
};

/// One line type: its `type`, what it is, its fields (beside `type`).
struct LineSpec {
    std::string_view type;
    std::string_view description;
    std::span<const FieldSpec> fields;
};

/// Every event machine mode can write, in the reference's order.
[[nodiscard]] std::span<const LineSpec> machine_events() noexcept;

/// Every line a driven session may read on stdin.
[[nodiscard]] std::span<const LineSpec> machine_inbound() noexcept;

/// The `type`s of `machine_events()`, in order.
[[nodiscard]] std::span<const std::string_view> machine_event_types() noexcept;

/// The `type`s of `machine_inbound()`, in order.
[[nodiscard]] std::span<const std::string_view> machine_inbound_types() noexcept;

/// The declaration as a JSON Schema (draft 2020-12): the root validates one
/// line the child writes; `#/$defs/inbound` one line a driver writes. Each
/// known type is held to its definition, an unknown one only to being an
/// object with a string `type`; every definition admits fields it does not
/// name. Carries `kMachineSchemaVersion` as `x-apogee.schema`.
[[nodiscard]] nlohmann::json machine_schema();

}  // namespace apogee::commands
