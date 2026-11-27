import BusinessReads
import DataPreferences
import DataProtocol
import Foundation
import Testing

@testable import PresentationVoice

/// Voice's lines with every positional after `--` (GUI M23): a voice model's
/// handle read by `models info -- <handle>`, and the text spoken after the
/// `--` it has had since 4g.
@Suite("Voice names", .timeLimit(.minutes(1)))
@MainActor
struct VoiceNamesTests {
    static let binary = URL(fileURLWithPath: "/Users/someone/.local/bin/apogee")
    static let handle = "Kokoro-82M/voice/5f3a2c91d0e4"

    func model(_ runner: ScriptedRunner) -> VoiceViewModel {
        VoiceViewModel(runner: runner, starter: ScriptedJobs([]), preferences: Preferences(store: MemoryStore()), home: URL(fileURLWithPath: "/Users/someone"))
    }

    @Test("the voice model's voices are read by models info -- <handle>; before -- the read is the parser's to refuse")
    func voicesAfterTheEnd() async {
        let runner = ScriptedRunner()
        runner.reading(["models", "list"], VoiceViewModelTests.recorded("list"))
        runner.reading(["models", "info", "--", Self.handle], VoiceViewModelTests.recorded("info-voice"))
        let voice = model(runner)
        await voice.connect(binary: Self.binary)
        #expect(runner.calls.contains("read models info -- \(Self.handle)"))
        #expect(!voice.voices.isEmpty, "the voices the model lists")
    }

    @Test("every line Voice builds with -x, -h or --graph reaches the verb: each recorded from the built CLI, none the parser's refusal")
    func sweep() async {
        var lines: [[String]] = []
        for name in NameVerdicts.names {
            let runner = ScriptedRunner()
            runner.reading(["models", "list"], VoiceViewModelTests.recorded("list"))
            let voice = model(runner)
            await voice.connect(binary: Self.binary)
            voice.text = name
            lines += [voice.plannedArguments ?? [], Argv.adding(["--output-format", "json"], to: Argv.line(["models", "info"], names: [name]))]
        }
        let (missing, refused) = NameVerdicts.judged(lines)
        #expect(missing.isEmpty, "never recorded -- add these rows to tests/presentation/shell/Fixtures/argv-name-verdicts.txt and run scripts/refresh-argv-name-verdicts.sh <the built apogee>:\n\(missing.joined(separator: "\n"))")
        #expect(refused.isEmpty, "the parser refuses:\n\(refused.joined(separator: "\n"))")
    }
}
