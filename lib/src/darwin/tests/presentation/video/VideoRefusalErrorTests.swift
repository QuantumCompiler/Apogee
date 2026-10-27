import BusinessJobs
import BusinessMedia
import DataPreferences
import DataProtocol
import Foundation
import InfrastructureProcess
import Testing

@testable import PresentationVideo

/// GUI M13: since the CLI's M29 a runtime refusal ends the job's stream on an
/// `error` event, its words still on stderr. The consent reads the stream's
/// `error` first and stderr only where no `error` arrived -- over the branch
/// binary's own recording (`generate-refused-runtime-m29`), and 4f's from a
/// binary before it, whose stream says only its `job` line.
@Suite("VideoRefusalError (M13)", .timeLimit(.minutes(1)))
@MainActor
struct VideoRefusalErrorTests {
    static let binary = URL(fileURLWithPath: "/Users/someone/.local/bin/apogee")

    func refused(_ recording: String) async throws -> (VideoViewModel, URL) {
        let home = FileManager.default.temporaryDirectory.appendingPathComponent("video-error-\(UUID().uuidString)")
        try FileManager.default.createDirectory(at: home.appendingPathComponent("Movies/Apogee"), withIntermediateDirectories: true)
        let first = ScriptedJob()
        let model = VideoViewModel(starter: ScriptedJobs([first]), preferences: Preferences(store: MemoryStore()), home: home)
        model.streams = { true }
        model.connect(binary: Self.binary)
        model.newClip()
        model.form.prompt = "a heron lands on a pier at dusk"
        model.generate()
        first.replay(
            stdout: recordedFixture("business/media/Fixtures/\(recording).jsonl"),
            stderr: recordedFixture("business/media/Fixtures/\(recording).err"), status: 1, streaming: true)
        _ = await eventually { model.consent != nil }
        return (model, home)
    }

    @Test("the stream's error names the install: the consent is the CLI's words there, as the stream wrote them")
    func fromTheStreamsError() async throws {
        let (model, home) = try await refused("generate-refused-runtime-m29")
        defer { try? FileManager.default.removeItem(at: home) }
        let error = try #require(Recorded.error(in: recordedFixture("business/media/Fixtures/generate-refused-runtime-m29.jsonl")))
        #expect(model.consent?.arguments == ["train", "setup", "--with", "video"])
        #expect(model.consent?.said == error)
    }

    @Test("a binary before M29 -- its stream silent -- still asks, from stderr's words")
    func olderFromStderr() async throws {
        let (model, home) = try await refused("generate-refused-runtime")
        defer { try? FileManager.default.removeItem(at: home) }
        #expect(model.consent?.arguments == ["train", "setup", "--with", "video"])
        #expect(model.consent?.said == "apogee video generate: torch and diffusers are not installed in Apogee's Python environment -- apogee train setup --with video")
    }
}

enum Recorded {
    /// The first line of a recorded stream that is an `error` event: its message.
    static func error(in stream: String) -> String? {
        stream.split(separator: "\n").lazy.compactMap { line -> String? in
            if case .error(let message) = JobEvent.decode(line: String(line)) { message } else { nil }
        }.first
    }
}
