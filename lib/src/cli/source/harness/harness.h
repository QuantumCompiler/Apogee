#pragma once

/// Harness -- the backend-agnostic core.
///
/// Reserved by the project skeleton; filled by the `harness-core` backlog item
/// with the LLMProvider interface, the canonical message IR every backend
/// translates to and from, and the router that picks a provider from config.
///
/// This is the package that makes "backend-agnostic core" (SPEC.md ->
/// Principles) true: everything above it speaks the IR, everything below it
/// speaks a vendor's wire format, and nothing crosses.
namespace apogee::harness {}
