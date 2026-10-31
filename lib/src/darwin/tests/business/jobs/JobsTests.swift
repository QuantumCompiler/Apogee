import BusinessJobs
import DataProtocol
import Foundation
import InfrastructureProcess
import Testing

/// A fake `apogee` long run: a shell script that prints what each case needs,
/// on stdout and stderr, and can be interrupted.
final class FakeRun {
    let directory: URL

    init() throws {
        directory = FileManager.default.temporaryDirectory.appendingPathComponent("apogee-job-\(UUID().uuidString)")
        try FileManager.default.createDirectory(at: directory, withIntermediateDirectories: true)
    }

    deinit { try? FileManager.default.removeItem(at: directory) }

    func binary(_ script: String) throws -> URL {
        let file = directory.appendingPathComponent("apogee")
        try ("#!/bin/sh\n" + script + "\n").write(to: file, atomically: true, encoding: .utf8)
        try FileManager.default.setAttributes([.posixPermissions: 0o755], ofItemAtPath: file.path)
        return file
    }

    func updates(_ job: any JobRunning) async -> [JobUpdate] {
        var all: [JobUpdate] = []
        for await update in job.updates { all.append(update) }
        return all
    }
}

@Suite("Jobs", .timeLimit(.minutes(1)))
struct JobsTests {
    @Test("the 52b stream's events are read typed, the unknown kept, the document whole")
    func events() {
        #expect(JobEvent.decode(line: #"{"type":"job","verb":"models pull","protocol_version":1,"schema":"2026-10-20"}"#) == .job(verb: "models pull"))
        #expect(JobEvent.decode(line: #"{"type":"stage","index":1,"count":3,"label":"pull org/model's full weights","state":"started"}"#)
            == .stage(index: 1, count: 3, label: "pull org/model's full weights", state: "started"))
        #expect(JobEvent.decode(line: #"{"type":"bytes","done":67108864,"file":"model-00001-of-00002.safetensors","index":1,"count":4}"#)
            == .bytes(done: 67_108_864, total: nil, file: "model-00001-of-00002.safetensors", index: 1, count: 4), "no total: none guessed")
        #expect(JobEvent.decode(line: #"{"type":"step","step":3,"steps":50,"scene":2}"#) == .step(step: 3, steps: 50, scene: 2))
        #expect(JobEvent.decode(line: #"{"type":"segment","index":1,"of":2,"seconds":2.4}"#) == .segment(index: 1, of: 2, seconds: 2.4, scene: nil))
        #expect(JobEvent.decode(line: #"{"type":"scene","scene":1,"scenes":2,"state":"speaking"}"#) == .scene(scene: 1, scenes: 2, state: "speaking"))
        #expect(JobEvent.decode(line: #"{"type":"notice","text":"pulling org/model"}"#) == .notice("pulling org/model"))
        #expect(JobEvent.decode(line: #"{"type":"error","message":"cancelled"}"#) == .error("cancelled"))
        #expect(JobEvent.decode(line: #"{"type":"document","document":{"name":"org--model/gguf/9f8e"}}"#)
            == .document(.object(["name": .string("org--model/gguf/9f8e")])))
        #expect(JobEvent.decode(line: #"{"type":"a_later_event","x":1}"#) == .other(type: "a_later_event", fields: ["type": .string("a_later_event"), "x": .number(1)]))
        #expect(JobEvent.decode(line: #"{"type":"stage"}"#) == .other(type: "stage", fields: ["type": .string("stage")]), "a stage with no index is kept, not guessed")
    }

    @Test("a pull's progress folds from its stream: stages, bytes, notices, documents, done")
    func progressStreamed() {
        var progress = JobProgress()
        for line in [
            #"{"type":"job","verb":"models pull"}"#,
            #"{"type":"notice","text":"pulling org/model, then converting it to F16 and registering it"}"#,
            #"{"type":"stage","index":1,"count":3,"label":"pull org/model's full weights","state":"started"}"#,
            #"{"type":"bytes","done":50,"total":200,"file":"a.safetensors","index":1,"count":2}"#,
        ] {
            progress.apply(.event(JobEvent.decode(line: line)))
        }
        #expect(progress.streamed)
        #expect(progress.bytes?.fraction == 0.25)
        #expect(progress.unfinishedStage == 1)
        progress.apply(.event(.stage(index: 1, count: 3, label: "pull org/model's full weights", state: "done")))
        progress.apply(.event(.stage(index: 2, count: 3, label: "convert to F16", state: "started")))
        #expect(progress.bytes == nil, "a new stage's bytes start again")
        progress.apply(.event(.document(.object(["name": .string("org--model/gguf/9f8e")]))))
        progress.apply(.ended(JobEnd(exit: ChildExit(reason: .exited, status: 0), stderrTail: BoundedTail(), interrupted: false)))
        #expect(progress.status == .done)
        #expect(progress.documents.count == 1)
        #expect(progress.orderedStages.map(\.index) == [1, 2])
    }

    @Test("stopped, the running stage is marked stopped; a failure keeps the stream's own words")
    func stoppedAndFailed() {
        var stopped = JobProgress()
        stopped.apply(.event(.stage(index: 2, count: 3, label: "convert to F16", state: "started")))
        stopped.apply(.ended(JobEnd(exit: ChildExit(reason: .exited, status: 130), stderrTail: BoundedTail(), interrupted: true)))
        #expect(stopped.status == .stopped)
        #expect(stopped.stages[2]?.state == "stopped")

        var failed = JobProgress()
        failed.apply(.event(.stage(index: 3, count: 3, label: "quantize to Q4_K_M", state: "failed")))
        failed.apply(.event(.notice("stopped at step 3 of 3 -- carry on with: apogee models quantize org--model/gguf/F16 Q4_K_M")))
        failed.apply(.event(.error("quantize failed: out of disk space")))
        failed.apply(.ended(JobEnd(exit: ChildExit(reason: .exited, status: 1), stderrTail: BoundedTail(), interrupted: false)))
        #expect(failed.status == .failed("quantize failed: out of disk space"))
        #expect(failed.resumeCommand == ["models", "quantize", "org--model/gguf/F16", "Q4_K_M"])

        var silent = JobProgress()
        var tail = BoundedTail()
        tail.append(Data("apogee: no such repository\n".utf8))
        silent.apply(.ended(JobEnd(exit: ChildExit(reason: .exited, status: 1), stderrTail: tail, interrupted: false)))
        #expect(silent.status == .failed("apogee: no such repository"))
        #expect(silent.resumeCommand == nil)
    }

    @Test("without the stream, stdout is what it says for people and stderr's last line is the busy line")
    func fallback() async throws {
        let fake = try FakeRun()
        let binary = try fake.binary(#"echo "pulling tiny:latest"; echo "downloading 12%" >&2; sleep 0.6; echo "registered tiny" "#)
        let job = JobStarter().startJob(binary: binary, arguments: ["models", "pull", "tiny:latest"], streaming: false)
        let updates = await fake.updates(job)
        #expect(updates.contains(.said("pulling tiny:latest")))
        #expect(updates.contains(.busy("downloading 12%")))
        guard case .ended(let end) = updates.last else {
            Issue.record("no end")
            return
        }
        #expect(end.succeeded && !end.interrupted)
    }

    @Test("Stop interrupts it as Ctrl-C does, and the end says it was stopped")
    func stop() async throws {
        let fake = try FakeRun()
        let binary = try fake.binary(#"trap 'echo "{\"type\":\"error\",\"message\":\"cancelled\"}"; exit 130' INT; echo '{"type":"job","verb":"models pull"}'; while true; do sleep 0.05; done"#)
        let job = JobStarter().startJob(binary: binary, arguments: [], streaming: true)
        // Stopped once its first line is out -- its trap is set by then --
        // never after a guessed wait.
        var updates: [JobUpdate] = []
        var stopping = false
        for await update in job.updates {
            updates.append(update)
            if !stopping, case .event(.job) = update {
                stopping = true
                Task { await job.stop(grace: .seconds(5)) }
            }
        }
        #expect(updates.first == .event(.job(verb: "models pull")))
        #expect(updates.contains(.event(.error("cancelled"))))
        guard case .ended(let end) = updates.last else {
            Issue.record("no end")
            return
        }
        #expect(end.interrupted)
        #expect(end.exit == ChildExit(reason: .exited, status: 130))
    }

    @Test("a binary that will not start ends at once, saying why")
    func couldNotStart() async {
        let job = JobStarter().startJob(binary: URL(fileURLWithPath: "/nonexistent/apogee"), arguments: [], streaming: true)
        var updates: [JobUpdate] = []
        for await update in job.updates { updates.append(update) }
        guard case .ended(let end) = updates.last else {
            Issue.record("no end")
            return
        }
        #expect(end.exit == nil && end.detail != nil)
    }
}
