import Foundation

/// The window's place changes, applied in the order they were made, each
/// once the one before it has finished (GUI 6b).
///
/// A change awaits the binary's re-ask before its place reads (GUI 4k), so a
/// second change made during that wait would otherwise run first: a session
/// paused before the first change resumed it, its child left running off
/// screen. Chained, the last change made is the one in force.
@MainActor
final class PlaceChanges {
    /// The change made last: the next one waits for it.
    private var last: Task<Void, Never>?

    /// Applies `change` once every change made before it has been applied.
    func apply(_ change: @escaping @MainActor () async -> Void) {
        let before = last
        last = Task {
            await before?.value
            await change()
        }
    }

    /// Returns once every change made so far -- and any made meanwhile --
    /// has been applied.
    func settled() async {
        var waited: Task<Void, Never>?
        while let pending = last, pending != waited {
            waited = pending
            await pending.value
        }
    }
}
