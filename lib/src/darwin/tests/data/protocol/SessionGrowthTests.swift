import DataProtocol
import Testing

/// The CLI's 52a and 52d growths, read and written: the excerpts a turn drew
/// on, the chats it recalled by name, a private session, and the three lines
/// a session takes since 52d.
@Suite("SessionGrowth")
struct SessionGrowthTests {
    @Test("a sources event: each excerpt's file and where in it, its score on its own retriever's scale")
    func sources() {
        let event = EventDecoder.decode(line: #"{"type":"sources","turn":2,"sources":[{"file":"report.pdf","pages":[41,42],"score":0.81,"retriever":"vector"},{"file":"notes.md","lines":[12,12],"score":0.4,"retriever":"lexical"},{"file":"talk.m4a","times":[30,65.5]},{"pages":[1]}]}"#)
        guard case .sources(let sources) = event.kind else {
            Issue.record("not sources: \(event.kind)")
            return
        }
        #expect(sources.map(\.file) == ["report.pdf", "notes.md", "talk.m4a"], "an excerpt with no file is skipped")
        #expect(sources[0] == Source(file: "report.pdf", pages: [41, 42], lines: nil, times: nil, score: 0.81, retriever: "vector"))
        #expect(sources.map(\.place) == ["pages 41–42", "line 12", "0:30–1:05"])
        #expect(event.turn == 2)
        #expect(EventDecoder.decode(line: #"{"type":"sources"}"#).kind == .unknown(type: "sources"), "a sources event with no list is kept, not guessed")
    }

    @Test("a memory event names its chats since 52a; a build before it counts them alone")
    func recalled() {
        let named = EventDecoder.decode(line: #"{"type":"memory","chats":1,"decisions":0,"sessions":[{"id":"20261019-142233-a1b2c3","name":"asyncio timeouts in the scraper","updated":"2026-10-19T14:40:02Z"}]}"#)
        #expect(named.kind == .memory(chats: 1, decisions: 0))
        #expect(named.recalled == [RecalledChat(id: "20261019-142233-a1b2c3", name: "asyncio timeouts in the scraper", updated: "2026-10-19T14:40:02Z")])
        #expect(EventDecoder.decode(line: #"{"type":"memory","chats":2,"decisions":1}"#).recalled.isEmpty)
    }

    @Test("a private session says so in its session event; one that does not, is not")
    func privateSession() {
        guard case .session(let info) = EventDecoder.decode(line: #"{"type":"session","protocol_version":1,"model":"m","next_turn":1,"private":true}"#).kind,
            case .session(let plain) = EventDecoder.decode(line: #"{"type":"session","protocol_version":1,"model":"m","next_turn":1}"#).kind
        else {
            Issue.record("not sessions")
            return
        }
        #expect(info.isPrivate)
        #expect(!plain.isPrivate)
    }

    @Test("detach, set and compact are written byte-exact, set's settings in the order the CLI applies them")
    func lines() {
        #expect(DriverLine.detach(path: "report.pdf").encoded == #"{"type":"detach","path":"report.pdf"}"#)
        #expect(DriverLine.compact.encoded == #"{"type":"compact"}"#)
        #expect(DriverLine.set(RunSettings(model: "claude", system: "Be brief.", think: "off", effort: "high", temperature: 0.2)).encoded
            == #"{"type":"set","model":"claude","system":"Be brief.","think":"off","effort":"high","temperature":0.2}"#)
        #expect(DriverLine.set(RunSettings(think: "auto")).encoded == #"{"type":"set","think":"auto"}"#)
    }

    @Test("a settings change is what differs; nothing unset is a change, and merging lays one over another")
    func settings() {
        let now = RunSettings(model: "local", think: "on")
        #expect(now.changes(to: RunSettings(model: "claude", think: "on")) == RunSettings(model: "claude"))
        #expect(now.changes(to: RunSettings(think: nil)).isEmpty, "clearing is not a change a session can make")
        #expect(now.changes(to: now).isEmpty)
        #expect(now.merging(RunSettings(effort: "high")) == RunSettings(model: "local", think: "on", effort: "high"))
    }
}
