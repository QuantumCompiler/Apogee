import BusinessSession
import DataProtocol
import Foundation
import InfrastructureProcess
import PresentationSession
import Testing

/// GUI M13, the reducer alone: what the `session` event says of the suite,
/// the launch's notices drawn once across a relaunch, and a launch refused --
/// over the branch binary's recordings since the CLI's M29, and 4i's before it.
@Suite("The transcript reads the suite and the launch (M13)")
struct TranscriptSuiteTests {
    /// `name`'s lines folded, `typed` sent once its session has spoken.
    static func folded(_ name: String, into transcript: inout Transcript, typed: String? = "Which suite answers?") {
        for line in fixtureStream(name) {
            let event = EventDecoder.decode(line: line)
            if let typed, event.turn != nil, !transcript.items.contains(where: { $0.content == .user(typed) && $0.turn == event.turn }) {
                transcript.userSent(typed)
            }
            transcript.apply(.event(event))
        }
    }

    static func notices(_ transcript: Transcript) -> [String] {
        transcript.items.compactMap { if case .notice(let text) = $0.content { text } else { nil } }
    }

    /// A launch the CLI refused, as recorded in `name`: exit 1, its stderr.
    static func refused(_ name: String) -> SessionEnd {
        var tail = BoundedTail()
        tail.append(Data(recorded("data/protocol/Fixtures/\(name).err").utf8))
        return SessionEnd(reason: .failed, exit: ChildExit(reason: .exited, status: 1), stderrTail: tail)
    }

    @Test("the session event's suite: named, none said by a binary that names its suite, unsaid by one before it")
    func suiteRead() {
        var named = Transcript()
        Self.folded("chat-under-suite-m29", into: &named)
        #expect(named.suite == .named("pair") && named.orchestrating == nil)
        var none = Transcript()
        Self.folded("resumed-suite-off-m29", into: &none)
        #expect(none.suite == .noSuite)
        var older = Transcript()
        Self.folded("chat-under-suite", into: &older)
        #expect(older.suite == nil, "2026-10-25: not said, never none")
        var execute = Transcript()
        Self.folded("execute-orchestrating", into: &execute)
        #expect(execute.suite == .named("orch") && execute.orchestrating == true)
    }

    @Test("kept through a pause and a relaunch until a child says otherwise; forgotten when the person moves the suite")
    func suiteKeptAndForgotten() {
        var transcript = Transcript()
        Self.folded("chat-under-suite-m29", into: &transcript)
        transcript.paused()
        transcript.reopening()
        #expect(transcript.suite == .named("pair"), "the conversation's, until the next child speaks")
        transcript.forgetSuite()
        #expect(transcript.suite == nil)
        Self.folded("resumed-switched-suite-m29", into: &transcript)
        #expect(transcript.suite == .named("fast"))
    }

    @Test("a relaunch saying again at launch what the child before it said draws it once; a different line, or one after a line sent, is drawn")
    func launchNoticesOnce() {
        var transcript = Transcript()
        Self.folded("chat-under-suite-m29", into: &transcript)
        transcript.reopening()
        Self.folded("resumed-under-suite-m29", into: &transcript)
        #expect(Self.notices(transcript) == ["suite pair: no member holds memory on this machine"])
        transcript.reopening()
        Self.folded("resumed-switched-suite-m29", into: &transcript)
        #expect(Self.notices(transcript).last == "suite fast: no member holds memory on this machine", "another suite's admission")
        transcript.reopening()
        transcript.apply(.event(EventDecoder.decode(line: fixtureStream("resumed-switched-suite-m29")[0])))
        transcript.lineSent()
        transcript.apply(.event(EventDecoder.decode(line: #"{"type":"notice","text":"suite fast: no member holds memory on this machine"}"#)))
        #expect(Self.notices(transcript).filter { $0.hasPrefix("suite fast") }.count == 2, "after a line, it answers the line")
    }

    @Test("restarted afresh, the notices carried and the last launch's lines known: a child saying them again draws them once")
    func restartedKeepsTheLaunch() {
        var transcript = Transcript()
        Self.folded("chat-under-suite-m29", into: &transcript, typed: nil)
        var fresh = transcript.restarted()
        #expect(Self.notices(fresh) == ["suite pair: no member holds memory on this machine"])
        Self.folded("chat-under-suite-m29", into: &fresh, typed: nil)
        #expect(Self.notices(fresh) == ["suite pair: no member holds memory on this machine"])
    }

    @Test("a child that fails before its session event refused the launch: its words alone, the stderr's last line")
    func refusedAtLaunch() throws {
        var transcript = Transcript()
        transcript.apply(.ended(Self.refused("launch-refused-model")))
        let refusal = try #require(transcript.launchRefusal)
        #expect(refusal.atLaunch && refusal.reason == .failed)
        #expect(refusal.message == recorded("data/protocol/Fixtures/launch-refused-model.err").trimmingCharacters(in: .whitespacesAndNewlines))
        // A relaunch refused after turns: the turns stay, the refusal last.
        var resumed = Transcript()
        Self.folded("chat-on-good", into: &resumed)
        resumed.reopening()
        resumed.apply(.ended(Self.refused("launch-refused-resume")))
        #expect(resumed.launchRefusal?.message.hasPrefix("apogee chat: backend 'mlxgone' is configured but could not be started") == true)
        #expect(resumed.items.contains { if case .answer = $0.content { true } else { false } })
    }

    @Test("a failure once the session spoke, or a refusal saying nothing, is no launch refusal: ended, as before")
    func notARefusal() {
        var spoke = Transcript()
        Self.folded("chat-on-good", into: &spoke)
        spoke.apply(.ended(Self.refused("launch-refused-model")))
        #expect(spoke.launchRefusal == nil)
        var silent = Transcript()
        silent.apply(.ended(SessionEnd(reason: .failed, exit: ChildExit(reason: .exited, status: 1), stderrTail: BoundedTail())))
        #expect(silent.launchRefusal == nil, "no words to draw it in")
    }
}
