#pragma once

/// Secrets -- the provider credential store and the one key resolver.
///
/// Filled by the `provider-credential-store` item (Milestone U): a `0600`
/// JSON file beside the config with one slot per API-billing provider type
/// (`store.h`), and the single precedence chain every consumer resolves a key
/// through -- the entry's `api_key`, then the stored slot, then a conventional
/// environment variable read from a snapshot taken once (`resolve.h`).
///
/// The package is a leaf beside the harness: it includes `harness/config.h`
/// for the backend types and nothing from `backends/`, `commands/` or
/// `httpserver/`, and `harness.layering` holds it to that.
///
/// This umbrella header is what `tests/packages_test.cpp` includes; it names
/// the package's own headers so a broken include path fails the day it breaks.

#include "secrets/resolve.h"
#include "secrets/store.h"
