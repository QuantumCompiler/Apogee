#pragma once

/// HTTP server -- the `apogee serve` surface.
///
/// Filled by the `serve-public-plane` item (Milestone T): the OpenAI-compatible
/// inference plane over the shared agent loop, the third `Reporter` adapter
/// (`sse_reporter.h`), server-owned sessions persisted as ordinary chat
/// sessions (`session.h`), a listener-free route table (`mux.h`,
/// `handler.h`), and the one file that may open a port (`serve.h`).
///
/// Scope constraint, decided 2026-08-24: this package serves SERVER
/// DEPLOYMENTS only -- a remote mobile or desktop client making REST calls to
/// an Apogee running on a server. It is never a localhost backend for a local
/// front-end. Interactive turns are pipes on every backend, and the GUI powers
/// the CLI over stdin/stdout (SPEC.md -> Principles: "interactive = pipes,
/// serving = server"). Nothing in this package may become load-bearing for a
/// local `chat` or `complete`.
///
/// This umbrella header is what `tests/packages_test.cpp` includes; it names
/// the package's own headers so a broken include path fails the day it breaks.

#include "httpserver/handler.h"
#include "httpserver/http_types.h"
#include "httpserver/mux.h"
#include "httpserver/serve.h"
#include "httpserver/session.h"
#include "httpserver/sse_reporter.h"
#include "httpserver/sse_writer.h"
