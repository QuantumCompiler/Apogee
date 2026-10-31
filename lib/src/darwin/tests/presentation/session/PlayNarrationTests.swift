import BusinessSession
import DataProtocol
import Foundation
import PresentationSession
import Testing

/// A play narrated in the transcript from the recorded `execute` streams
/// (tests/data/protocol/Fixtures/execute-*.jsonl, the installed binary's
/// stdout over a scratch home with scripted mock members): the lines kept as
/// the CLI wrote them, read only to say which stage it is on.
@Suite("PlayNarration")
struct PlayNarrationTests {
    func lines(_ name: String) -> [String] {
        let fixture = URL(fileURLWithPath: #filePath)
            .deletingLastPathComponent().deletingLastPathComponent().deletingLastPathComponent()
            .appendingPathComponent("data/protocol/Fixtures/\(name).jsonl")
        return ((try? String(contentsOf: fixture, encoding: .utf8)) ?? "").split(separator: "\n").map(String.init)
    }

    /// The transcript after `name`'s stream, the person having sent `typed`.
    func folded(_ name: String, typed: String) -> Transcript {
        var transcript = Transcript()
        for line in lines(name) {
            transcript.apply(decoded(line))
            if JSONValue.parse(line)?["type"]?.stringValue == "session" {
                transcript.userSent(typed)
            }
        }
        return transcript
    }

    func activity(_ transcript: Transcript) -> Activity? {
        for item in transcript.items {
            if case .activity(let activity) = item.content { return activity }
        }
        return nil
    }

    @Test("a /play turn: its stages in order, named by the typed line, the output the answer")
    func typedPlay() throws {
        let transcript = folded("execute-play", typed: "/play echo2 hello")
        let activity = try #require(activity(transcript))
        let play = try #require(activity.play)
        #expect(play.symphony == "echo2")
        #expect(play.chosen == nil)
        #expect(play.stages.map(\.name) == ["first", "second"])
        #expect(play.stages.map(\.role) == ["utility", "chat"])
        #expect(play.stages.map(\.backend) == ["helper", "root"])
        #expect(play.stages[0].line == "stage 1/2 first — asking utility (helper): One: hello", "the line, whole")
        #expect(activity.lines == play.stages.map(\.line), "every line kept")
        #expect(!activity.live)
        #expect(play.summary(live: false) == "played echo2 · 2 stages")
        #expect(play.summary(live: true) == "playing echo2 · stage 2 of 2")
        guard case .answer(let answer) = transcript.items.last?.content else {
            Issue.record("no answer")
            return
        }
        #expect(answer.text == "ROOT<Two: HELPER<One: hello>>")
    }

    @Test("the model's own play (--orchestrate): the choice line names it, then its stages, then the answer it read")
    func orchestrated() throws {
        let transcript = folded("execute-orchestrate", typed: "go")
        let activity = try #require(activity(transcript))
        let play = try #require(activity.play)
        #expect(play.symphony == "echo2")
        #expect(play.chosen == "play — the model chose echo2: 2 member calls, utility → chat")
        #expect(play.stages.count == 2)
        #expect(activity.lines.first == "[tool] play_echo2", "the tool line stays, as written")
        #expect(play.summary(live: false) == "played echo2 · 2 stages")
    }

    @Test("a play that cannot run ends the turn in the CLI's words; no narration is made up")
    func refused() {
        let transcript = folded("execute-play-refused", typed: "/play nosuch hello")
        #expect(activity(transcript) == nil)
        guard case .error(let message) = transcript.items.last?.content else {
            Issue.record("no error")
            return
        }
        #expect(message.hasPrefix("no symphony named 'nosuch' (there are: "))
    }

    @Test("a project opening is said by the session's own notices: the persona, the context file, the folder attached")
    func project() {
        let transcript = folded("execute-project", typed: "hi")
        let notices = transcript.items.compactMap { item -> String? in
            if case .notice(let text) = item.content { text } else { nil }
        }
        #expect(notices.prefix(3) == [
            "persona: coder, the shipped coding prompt, in every request -- -s replaces it",
            "project context: AGENTS.md, 11 words, in every request",
            "attaching . (2 files, 89 bytes)",
        ])
        #expect(activity(transcript)?.play == nil)
    }

    @Test("a chain's stage and a line of something else: the position kept, anything else no stage")
    func lineShapes() throws {
        let chain = try #require(PlayNarration(
            lines: ["digest → summarize-verify, stage 1/2 summarize — asking utility (l3b): Summarize this"],
            typed: "/play digest The minutes…"))
        #expect(chain.symphony == "digest")
        #expect(chain.stages.first?.chain == "digest → summarize-verify")
        #expect(chain.stages.first?.name == "summarize")
        #expect(PlayNarration(lines: ["[tool] read_file", "consult — asking utility (l3b): Is it?"], typed: "hi") == nil)
    }
}
