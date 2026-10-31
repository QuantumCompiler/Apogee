import DataProtocol
import Testing

@Suite("ReadDocument")
struct ReadDocumentTests {
    @Test("a list document reads its entries, its total and its problems")
    func list() throws {
        let document = try #require(
            ReadDocument(text: #"{"object":"list","data":[{"id":"a"},{"id":"b"}],"total":7,"problems":[{"file":"x"}]}"#))
        #expect(document.object == "list")
        #expect(document.entries?.count == 2)
        #expect(document.entries?.first?["id"]?.stringValue == "a")
        #expect(document.total == 7)
        #expect(document.problems.count == 1)
    }

    @Test("a document that is not one JSON object is not a document")
    func notADocument() {
        #expect(ReadDocument(text: "[1,2]") == nil)
        #expect(ReadDocument(text: "nothing") == nil)
        #expect(ReadDocument(text: #"{"rows":[],"ok":true}"#)?.object == nil)
    }
}
