#pragma once

/// HTTP server -- the `apogee serve` surface.
///
/// Reserved by the project skeleton; filled by the `serve-public-plane` backlog
/// item (OpenAI-compatible endpoints) and then `admin-plane-foundation`.
///
/// Scope constraint, decided 2026-08-24: this package serves SERVER
/// DEPLOYMENTS only -- a remote mobile or desktop client making REST calls to
/// an Apogee running on a server. It is never a localhost backend for a local
/// front-end. Interactive turns are pipes on every backend, and the GUI powers
/// the CLI over stdin/stdout (SPEC.md -> Principles: "interactive = pipes,
/// serving = server"). Nothing in this package may become load-bearing for a
/// local `chat` or `complete`.
namespace apogee::httpserver {}
