import BusinessReads
import DataProtocol
import Foundation
import Testing

/// Recorded from the installed binary over a scratch home (`Fixtures/`):
/// three records captured by a scripted clerk, a lexical collection of three
/// notes, and that collection's prose graph built by a scripted extractor,
/// its community summarised, a decision record captured into it.
private func document(_ name: String) -> ReadDocument {
    let url = URL(fileURLWithPath: #filePath).deletingLastPathComponent().appendingPathComponent("Fixtures/\(name).json")
    return ReadDocument(text: try! String(contentsOf: url, encoding: .utf8))!
}

@Suite("Knowledge")
struct KnowledgeTests {
    @Test("records are read as the list carries them: branch, discipline, link, provenance, newest first")
    func records() {
        let records = KnowledgeRecord.list(from: document("knowledge-list"))
        #expect(records.map(\.status) == ["shipped", "shipped", "rejected"])
        #expect(records.map(\.discipline) == ["product", "eng", "ux"])
        #expect(records[1].link == "PROJ-42" && records[0].link.isEmpty)
        #expect(records[2].decision == "Remove the cancel button from checkout.")
        #expect(records[2].source == "meeting" && records[2].attribution == "Ada Lovelace")
    }

    @Test("a question's answer keeps each score on its retriever's scale, and says which retriever ran")
    func query() throws {
        let query = try #require(KnowledgeQuery(document: document("knowledge-query")))
        #expect(query.retriever == "lexical" && !query.reranked)
        #expect(query.matches.count == 3)
        #expect(query.matches.first?.record.decision == "Remove the cancel button from checkout.")
        #expect(abs((query.matches.first?.score ?? 0) - 0.5963) < 0.001)
    }

    @Test("collections: the list's counts, and one collection's health and sources, a text-only one said so")
    func collections() throws {
        #expect(CollectionRow.list(from: document("collections-list")).map(\.name) == ["knowledge", "notes"])
        let info = try #require(CollectionInfo(document: document("collection-info")))
        #expect(info.name == "notes" && info.chunks == 5 && info.index == "ok" && info.vectors == nil)
        #expect(info.sources.contains("heat-pumps.md") && info.sources.contains("graph://community/1"))
    }

    /// GUI 6h: `embed info`'s `vectors` is an object -- recorded from the
    /// branch's `apogee-dev` over a sandbox home, three notes ingested with
    /// `--retriever vector` on a mock embedder (`mock-embed`), then a fourth
    /// under a second one (`mock-embed-b`) into another collection -- never
    /// a number, which the app once asked it for.
    @Test("a vectorised collection's vectors are embed info's object: chunks, dimension, widths, the model")
    func vectorisedCollection() throws {
        let info = try #require(CollectionInfo(document: document("collection-info-vectors")))
        let vectors = try #require(info.vectors, "an object, read as one")
        #expect(info.chunks == 3)
        #expect(vectors.chunks == 3 && vectors.dimension == 8 && vectors.widths == 1)
        #expect(vectors.model == "mock-embed" && vectors.mixed.isEmpty)
        let mixed = try #require(CollectionInfo(document: document("collection-info-vectors-mixed"))?.vectors)
        #expect(mixed.chunks == 4 && mixed.model == nil)
        #expect(mixed.mixed == ["mock-embed", "mock-embed-b"])
    }

    @Test("vectors: null is a text-only collection; a key a later CLI adds is tolerated; an unrecorded mixed model is nil")
    func vectorsTolerant() throws {
        #expect(CollectionInfo.Vectors(JSONValue.null) == nil)
        #expect(CollectionInfo.Vectors(nil) == nil)
        // Handmade from the recorded shape: a key no CLI sends yet, and a
        // mixed list with the CLI's `null` for an unrecorded model.
        let later = try #require(ReadDocument(text: #"{"chunks":2,"dimension":768,"widths":2,"mixed":[null,"nomic"],"quantized":true}"#))
        let vectors = try #require(CollectionInfo.Vectors(later.value))
        #expect(vectors == CollectionInfo.Vectors(chunks: 2, dimension: 768, widths: 2, model: nil, mixed: [nil, "nomic"]))
    }

    @Test("a prose graph: its stats say collection, its report names the community, the decision and the hubs, its explain both")
    func proseGraph() throws {
        let stats = GraphStats(document: document("graph-prose-stats"))
        #expect(stats.isProse && stats.built && stats.communities == 1)
        let report = try #require(GraphReport(document: document("graph-prose-report")))
        #expect(report.communities.map(\.size) == [3] && report.communities.first?.members.count == 3)
        #expect(report.communities.first?.summary?.hasPrefix("The heat pump") == true)
        #expect(report.decisions.map(\.status) == ["shipped"] && report.decisionsTotal == 1)
        #expect(report.hubs.first?.node.name == "Heat pump" && report.hubs.first?.degree == 3)
        let explain = try #require(GraphExplain(document: document("graph-prose-explain")))
        #expect(explain.communities.map(\.id) == [1])
        #expect(explain.decisions.first?.decision.hasPrefix("Explain the outdoor unit") == true)
        #expect(explain.decisions.first?.discipline == "product")
    }
}
