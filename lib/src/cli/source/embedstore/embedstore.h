#pragma once

/// Embed store -- chunk storage and retrieval for RAG.
///
/// Reserved by the project skeleton; filled by the `embedstore-lexical-rag`
/// backlog item with the SQLite chunk store and FTS5/BM25 lexical retrieval,
/// then extended by `embedding-clients` and `vector-hybrid-rerank`.
///
/// The ordering is deliberate: the lexical floor lands first and works with no
/// embedding model at all, so retrieval never becomes a reason to require one.
namespace apogee::embedstore {}
