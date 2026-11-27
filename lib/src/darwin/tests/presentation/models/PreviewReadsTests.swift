import BusinessReads
import Foundation
import Testing

@testable import PresentationModels

/// The previews' recorded reads answer the lines the view-models build (GUI
/// M23): a backend's, a set's or a path's `models info`, a delete's plan and
/// a registration are keyed by prefixes of their lines, so a name that moves
/// after `--` would otherwise leave the preview drawing an empty state, or a
/// shorter prefix's document, and fail nothing. Each preview home is read by
/// both tabs, each row and set it records a document for selected, and its
/// stories told as its previews tell them. Both tabs read more of a home
/// than its previews do -- a Backends home's paths, a store home's backends
/// -- so a read no rule answers here is no preview's empty state. What is
/// held is the shape: a read no rule answers though the home records its
/// words under another prefix fails, and so does a prefix holding a name
/// that no line reaches.
@Suite("Models preview reads", .timeLimit(.minutes(2)))
@MainActor
struct PreviewReadsTests {
    /// Both tabs over `runner`: connected, read, and each row and set its
    /// home records a document for selected, as its previews select them.
    func clickThrough(_ runner: some CommandRunning, _ log: PreviewReadLog) async {
        let models = ModelsViewModel(runner: runner)
        await models.connect(binary: previewBinary)
        for row in models.rows where log.holds(Argv.line(["models", "info"], names: [row.backend])) {
            await models.select(row)
            if models.offersDrafterPairing(row) { await models.askToPairDrafter(row) }
            await models.askToDelete(row)
        }
        let store = StoreViewModel(runner: runner)
        await store.connect(binary: previewBinary)
        await store.refresh()
        for set in store.sets where log.holds(Argv.line(["models", "info"], names: [set.handle])) {
            await store.select(set.handle)
            await store.askToPair(set)
            await store.askToDelete(set)
        }
    }

    @Test("every prefix the Models previews hold for a name is the line's shape, and every one holding a name after -- is one a line reaches")
    func previewReads() async {
        let log = PreviewReadLog()
        await PreviewReadLog.$current.withValue(log) {
            let runners: [any CommandRunning] = [
                ModelsPreview.backendsRunner, ModelsPreview.adaptersRunner, ModelsPreview.storeRunner, ModelsPreview.conversionsRunner,
                ModelsPreview.kindsRunner, ModelsPreview.kindsRegisteredRunner, ModelsPreview.chatOnEmbeddingRunner,
                ModelsPreview.describedSuiteRunner, ModelsPreview.draftsRunner(paired: false), ModelsPreview.draftsRunner(paired: true),
                ModelsPreview.basePairRunner(paired: false), ModelsPreview.basePairRunner(paired: true), ModelsPreview.draftGoneRunner,
                ModelsPreview.draftRebuiltRunner, ModelsPreview.routesRunner, ModelsPreview.rowsDraftsRunner,
                ModelsPreview.quantizeStore(), ModelsPreview.removeStore(), ModelsPreview.deleteRolesStore(), ModelsPreview.addStore(),
                ModelsPreview.deletesStore(), ModelsPreview.backendsDeleteStore(),
            ]
            for runner in runners { await clickThrough(runner, log) }
            let added = ModelsPreview.addStore()
            let adding = StoreViewModel(runner: added)
            await adding.connect(binary: previewBinary)
            await ModelsPreview.registerTheFirst(adding, added)
            let deletes = ModelsPreview.deletesStore()
            let deleting = StoreViewModel(runner: deletes)
            await deleting.connect(binary: previewBinary)
            await ModelsPreview.deleteKept(deleting, deletes)
            let withBackends = ModelsPreview.deletesStore()
            let deletingAll = StoreViewModel(runner: withBackends)
            await deletingAll.connect(binary: previewBinary)
            await ModelsPreview.deleteWithBackends(deletingAll, withBackends)
            let removes = ModelsPreview.removeStore()
            let removing = ModelsViewModel(runner: removes)
            await removing.connect(binary: previewBinary)
            await ModelsPreview.removeTheDefault(removing, removes)
            let roles = ModelsPreview.deleteRolesStore()
            let roleHolder = ModelsViewModel(runner: roles)
            await roleHolder.connect(binary: previewBinary)
            await ModelsPreview.deleteTheRoleHolder(roleHolder, roles)
            for withBackends in [false, true] {
                let backends = ModelsPreview.backendsDeleteStore()
                let deletingBackend = ModelsViewModel(runner: backends)
                await deletingBackend.connect(binary: previewBinary)
                if withBackends {
                    await ModelsPreview.deleteBackendWithBackends(deletingBackend, backends)
                } else {
                    await ModelsPreview.deleteBackendKept(deletingBackend, backends)
                }
            }
            let quantized = ModelsPreview.quantizeStore()
            await ModelsPreview.runAgain(ModelsPreview.quantizing(quantized), quantized)
        }
        #expect(log.misshapen.isEmpty, "reads whose words the home records under another prefix: \(log.misshapen.map { $0.joined(separator: " ") })")
        #expect(log.unreached.isEmpty, "prefixes no line reaches: \(log.unreached.map { $0.joined(separator: " ") })")
    }
}
