#pragma once

/// Embed store -- chunk storage and retrieval for RAG.
///
/// Filled by the `embedstore-lexical-rag` item (Milestone Q): a SQLite chunk
/// store with an external-content FTS5 index (`store.h`), BM25 scores
/// normalized onto a comparable 0..1 scale (`fts.h`), codepoint-safe chunking
/// (`chunk.h`), and the directory walk that gets documents in (`ingest.h`).
/// Extended later by `embedding-clients` and `vector-hybrid-rerank`.
///
/// The ordering is deliberate: the lexical floor landed first and works with no
/// embedding model at all, so retrieval never becomes a reason to require one.
/// That is why every `SearchHit` carries the name of the retriever that scored
/// it -- once a second retriever exists, the scales are not comparable.
///
/// This umbrella header is what `tests/packages_test.cpp` includes; it names
/// the package's own headers so a broken include path fails the day it breaks.

#include "embedstore/chunk.h"
#include "embedstore/fts.h"
#include "embedstore/ingest.h"
#include "embedstore/store.h"
