import DataProtocol
import Foundation
import Testing

/// `apogee __machine-schema`, recorded with no config and no backend
/// (`Fixtures/`): the branch's binary (2026-10-25), the installed one
/// (2026-10-24), and one hand-made to the CLI source's shape before its track
/// 52 (v0.1.7's `protocol.cpp`, 2026-10-13: its line types kept, the rest cut).
private func recorded(_ name: String) -> String {
    let url = URL(fileURLWithPath: #filePath).deletingLastPathComponent().appendingPathComponent("Fixtures/\(name).json")
    return (try? String(contentsOf: url, encoding: .utf8)) ?? ""
}

@Suite("MachineSchema (GUI 4k)")
struct MachineSchemaTests {
    @Test("the branch's vocabulary: its date, its protocol, every event it writes and every line it reads")
    func branch() throws {
        let schema = try #require(MachineSchema(text: recorded("machine-schema")))
        #expect(schema.schema == "2026-10-25" && schema.protocolVersion == 1)
        #expect(schema.events.contains("job") && schema.events.contains("iteration") && schema.events.contains("session"))
        #expect(schema.accepts == ["answer", "attach", "cancel", "compact", "detach", "hello", "set", "user"])
        #expect(!schema.events.contains("outbound") && !schema.accepts.contains("inbound"), "the directions are no types")
    }

    @Test("older shapes say less: 2026-10-24 has 52's lines without 55h's iteration; before 52, no job and no set")
    func older() throws {
        let installed = try #require(MachineSchema(text: recorded("machine-schema-older")))
        #expect(installed.schema == "2026-10-24")
        #expect(installed.events.contains("job") && !installed.events.contains("iteration"))
        #expect(installed.accepts.contains("set"))
        let pre52 = try #require(MachineSchema(text: recorded("machine-schema-pre52-handmade")))
        #expect(pre52.schema == "2026-10-13")
        #expect(!pre52.events.contains("job") && !pre52.events.contains("sources"))
        #expect(pre52.accepts == ["answer", "attach", "cancel", "hello", "user"])
    }

    @Test("what isn't a schema says nothing: no JSON, no $defs, no date; the date falls back to the $id's end")
    func tolerant() throws {
        #expect(MachineSchema(text: "") == nil)
        #expect(MachineSchema(text: "The following arguments were not expected") == nil)
        #expect(MachineSchema(text: #"{"title":"Apogee machine mode"}"#) == nil)
        #expect(MachineSchema(text: #"{"$defs":{"event_job":{}}}"#) == nil, "no date: nothing assumed")
        let fromID = try #require(MachineSchema(text: #"{"$id":"https://example/machine-schema/2026-10-20","$defs":{"event_job":{},"line_set":{},"x_new":{}}}"#))
        #expect(fromID.schema == "2026-10-20" && fromID.events == ["job"] && fromID.accepts == ["set"] && fromID.protocolVersion == nil)
    }
}
