import Foundation
import Testing

@testable import PresentationTasks

/// The previews' recorded reads answer the lines the view-model builds (GUI
/// M23): a preview's document is keyed by a prefix of the line, so a line
/// that moves -- an id after `--` -- would otherwise leave the preview drawing
/// an empty state, or a shorter prefix's document, and fail nothing. Each
/// preview home is driven as its previews drive it.
@Suite("Tasks preview reads", .timeLimit(.minutes(1)))
@MainActor
struct PreviewReadsTests {
    @Test("every read the Tasks previews ask is answered, and every prefix holding a name after -- is one a line reaches")
    func previewReads() async {
        let log = PreviewReadLog()
        await PreviewReadLog.$current.withValue(log) {
            let tasks = TasksPreview.tasks()
            await tasks.connect(binary: previewBinary)
            await tasks.select(TasksPreview.task)
            await tasks.choose(recipe: "digest-folder")
            let stopped = TasksPreview.stopped()
            await stopped.connect(binary: previewBinary)
            for id in [TasksPreview.interrupted, TasksPreview.exhausted] { await stopped.select(id) }
            let killed = TasksPreview.killedRows()
            await killed.connect(binary: previewBinary)
            await killed.select(TasksPreview.killed)
            let sweep = TasksPreview.sweep()
            await sweep.connect(binary: previewBinary)
            for id in [TasksPreview.uses, TasksPreview.stalled, TasksPreview.stalledGranted, TasksPreview.halted] { await sweep.select(id) }
        }
        #expect(log.unanswered.isEmpty, "reads no recorded document answers: \(log.unanswered)")
        #expect(log.unreached.isEmpty, "prefixes no line reaches: \(log.unreached)")
    }
}
