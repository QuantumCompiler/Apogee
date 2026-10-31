import DataProtocol
import Testing

@Suite("DriverLine")
struct DriverLineTests {
    @Test("every line is one JSON object, type first, the same bytes every time")
    func byteExact() {
        #expect(DriverLine.hello(clientName: "Apogee", clientVersion: "0.2.0", wants: ["tools"]).encoded
            == #"{"type":"hello","client":{"name":"Apogee","version":"0.2.0"},"wants":["tools"]}"#)
        #expect(DriverLine.user("what is 2+2?").encoded == #"{"type":"user","text":"what is 2+2?"}"#)
        #expect(DriverLine.answer("todo.md").encoded == #"{"type":"answer","text":"todo.md"}"#)
        #expect(DriverLine.attach(path: "src", graph: "off").encoded == #"{"type":"attach","path":"src","graph":"off"}"#)
        #expect(DriverLine.attach(path: "report.pdf", graph: nil).encoded == #"{"type":"attach","path":"report.pdf"}"#)
        #expect(DriverLine.cancel.encoded == #"{"type":"cancel"}"#)
    }

    @Test("text is escaped, so a line is always one line")
    func escaping() {
        #expect(DriverLine.user("line one\nsaid \"hi\" \\ café").encoded
            == #"{"type":"user","text":"line one\nsaid \"hi\" \\ café"}"#)
        #expect(!DriverLine.user("a\nb\rc").encoded.contains("\n"))
    }
}
