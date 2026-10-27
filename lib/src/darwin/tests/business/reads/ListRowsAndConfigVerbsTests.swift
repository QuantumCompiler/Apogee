import BusinessReads
import DataProtocol
import Foundation
import Testing

/// What a binary printed over a sandbox home, the home written
/// `/Users/someone` (`Fixtures/`, and the task rows beside the Tasks tests'):
/// the branch binary (schema 2026-10-26, the CLI's M32 and M33) and, where a
/// name says `older`, the installed 07e26c6d (schema 2026-10-25).
private func printed(_ name: String) -> String {
    let here = URL(fileURLWithPath: #filePath).deletingLastPathComponent()
    let url = name.hasPrefix("task-")
        ? here.appendingPathComponent("../../presentation/tasks/Fixtures/\(name)")
        : here.appendingPathComponent("Fixtures/\(name)")
    return (try? String(contentsOf: url, encoding: .utf8)) ?? ""
}

private func document(_ name: String) throws -> ReadDocument {
    try #require(ReadDocument(text: printed(name)), "\(name) is one JSON document")
}

/// GUI M16's reads: the facts the CLI's M32 put on list rows -- a task killed
/// mid-round, an `mlx` entry's drafter -- and what its M33's config verbs
/// say: one provider registered, the roles a change moves, the allowed hosts.
@Suite("List rows and config verbs as the CLI states them (GUI M16)")
struct ListRowsAndConfigVerbsTests {
    static let killed = "task-20261027-023023-2"
    static let exhausted = "task-20261027-023023"

    // MARK: - Task rows

    @Test("a task row says whether its task was killed mid-round: the killed one interrupted and not running, the others not")
    func taskRowSaysInterrupted() throws {
        let rows = TaskSummary.list(from: try document("task-list-killed.json"))
        let killed = try #require(rows.first { $0.id == Self.killed })
        #expect(killed.status == "running" && killed.interrupted == true)
        #expect(!killed.isRunning, "no process runs it: the row says so without its task status")
        let exhausted = try #require(rows.first { $0.id == Self.exhausted })
        #expect(exhausted.interrupted == false && !exhausted.isRunning)
        let live = TaskSummary.list(from: try document("task-list-live.json"))
        let running = try #require(live.first { $0.id == Self.killed })
        #expect(running.interrupted == false && running.isRunning, "while its process ran, the same row read running")
    }

    @Test("a row of a binary before M32 says nothing of it: interrupted unknown, read by its status as before")
    func olderTaskRowSaysNothing() throws {
        for name in ["task-list-killed-older.json", "task-list-interrupted.json"] {
            let rows = TaskSummary.list(from: try document(name))
            #expect(!rows.isEmpty)
            #expect(rows.allSatisfy { $0.interrupted == nil }, "\(name)")
            #expect(rows.filter(\.isRunning).map(\.status) == ["running"], "the killed task reads running, as it did")
        }
    }

    // MARK: - A drafter on its row

    @Test("an mlx entry's row carries its drafter: one that drafts, one whose drafter is gone with the CLI's words, one with none")
    func mlxRowCarriesDraft() throws {
        let rows = ModelRow.list(from: try document("rows-drafts-list.json"))
        func row(_ backend: String) throws -> ModelRow { try #require(rows.first { $0.backend == backend }) }
        #expect(try row("Gemma4-E4B-4bit").draft == ModelRow.Draft(
            path: "/Users/someone/.apogee/models/Gemma4-E4B-Assistant/mlx/4d5e6f708192", canDraft: true, refusal: nil))
        #expect(try row("Gemma4-12B-4bit").draft == ModelRow.Draft(
            path: "/Users/someone/.apogee/models/Gemma4-12B-Assistant/mlx/8192a3b4c5d6", canDraft: false,
            refusal: "there is no drafter at /Users/someone/.apogee/models/Gemma4-12B-Assistant/mlx/8192a3b4c5d6 -- 'apogee models convert <a draft head> --mlx' makes one"))
        #expect(try row("Qwen3-8B-4bit").draft == nil)
        #expect(rows.filter(\.isStored).allSatisfy { $0.draft == nil }, "stored weights name no drafter")
    }

    @Test("whether a list states drafters: by a row carrying one, else by the binary's date; a list without either says nothing")
    func statesDrafts() throws {
        let stating = ModelRow.list(from: try document("rows-drafts-list.json"))
        #expect(ModelRow.statesDrafts(stating, vocabulary: nil), "a row carries one: the field's presence")
        let unpaired = ModelRow.list(from: try document("remove-list.json"))
        #expect(unpaired.allSatisfy { $0.draft == nil })
        #expect(ModelRow.statesDrafts(unpaired, vocabulary: "2026-10-26"), "nothing paired on M32's binary")
        #expect(!ModelRow.statesDrafts(unpaired, vocabulary: nil), "a binary that hasn't said")
        let older = ModelRow.list(from: try document("rows-drafts-list-older.json"))
        #expect(older.allSatisfy { $0.draft == nil }, "the installed binary over the same home states none")
        #expect(!ModelRow.statesDrafts(older, vocabulary: "2026-10-25"))
    }

    // MARK: - One provider registered

    @Test("providers register's document: its registration and rosters, a line each, as its text prints them")
    func providerRegistration() throws {
        let codex = try #require(ProviderRegistration(document: try document("providers-register-codex.json")))
        #expect(codex.provider == "codex")
        #expect(codex.registration == ["registered codex (codex-cli)", "models.default set to 'codex' (none was set)"])
        #expect(codex.rosters == ["fetched codex-cli's models: 2 on the roster"])
        let text = printed("providers-register-codex.txt").split(whereSeparator: \.isNewline).map(String.init)
        #expect(codex.lines == text, "the text form says the same lines")
        let claude = try #require(ProviderRegistration(document: try document("providers-register-claude.json")))
        #expect(claude.lines == ["registered claude (claude-cli)"], "claude-cli has no roster to fetch")
        let ollama = try #require(ProviderRegistration(document: try document("providers-register-ollama-none.json")))
        #expect(ollama.registration.first?.hasPrefix("ollama: not registered -- an ollama-cli backend needs a model") == true)
        let one = try #require(ProviderRegistration(document: try document("providers-register-ollama-one.json")))
        #expect(one.lines == [
            "registered ollama (ollama-cli, with Ollama's one model, llama3.2:3b)", "models.default set to 'ollama' (none was set)",
        ], "Ollama's one model, picked by the CLI")
        let gemini = try #require(ProviderRegistration(document: try document("providers-register-gemini.json")))
        #expect(gemini.lines == ["nothing to register -- no provider was detected"], "one the last scan didn't find")
        #expect(ProviderRegistration(document: try document("providers-scan-fakes.json")) == nil, "another document is none")
    }

    @Test("Register passes the scan's own provider id, and every one the scan lists is an id providers register knows, as its refusal names them")
    func scanIDsAreRegisterIDs() throws {
        let refusal = printed("providers-register-nope.err")
        let known = try #require(refusal.components(separatedBy: "the ones apogee knows: ").last)
            .trimmingCharacters(in: .whitespacesAndNewlines).components(separatedBy: ", ")
        #expect(refusal.hasPrefix("apogee providers: no provider named 'nope'"))
        let scanned = (try document("providers-scan-fakes.json").entries ?? []).compactMap { $0["provider"]?.stringValue }
        #expect(!scanned.isEmpty && Set(scanned).isSubset(of: Set(known)), "\(scanned) of \(known)")
    }

    // MARK: - The roles a change moves

    @Test("the roles a change moves, in the CLI's words: the default set by an add, cleared by a delete; nothing else read as one")
    func roleChanges() {
        #expect(RoleChange.said(in: printed("add-backend-default.txt")) == [.set(field: "default", to: "Qwen3-8B-4bit")])
        #expect(RoleChange.said(in: printed("register-all-default.txt")) == [.set(field: "default", to: "Gemma4-12B-4bit")])
        #expect(RoleChange.said(in: printed("delete-backend-clears.txt")) == [.cleared(field: "default", named: "Gemma4-12B-4bit")])
        #expect(RoleChange.said(in: printed("delete-roles-and-backends.txt")) == [
            .cleared(field: "default", named: "Qwen3-8B-4bit"), .cleared(field: "default_utility", named: "Qwen3-8B-4bit"),
        ])
        for quiet in ["add-backend-second.txt", "add-backend-older.txt", "delete-backend-older.txt", "delete-roles-plan.txt"] {
            #expect(RoleChange.said(in: printed(quiet)).isEmpty, "\(quiet)")
        }
        #expect(RoleChange(line: "models.default = Gemma4-E4B-4bit") == nil, "config set-default's own line is the person's act")
    }

    // MARK: - The allowed hosts

    @Test("the allowed hosts, one a line, none when the list is empty")
    func allowedHosts() {
        #expect(AllowedHosts.list(printed("allowed-hosts.txt")) == ["docs.python.org", "developer.apple.com"])
        #expect(AllowedHosts.list(printed("allowed-hosts-after-delete.txt")) == ["developer.apple.com"])
        #expect(AllowedHosts.list(printed("allowed-hosts-empty.txt")) == [])
    }

    @Test("the verbs follow the binary's date: M33's 2026-10-26 has them, the installed 2026-10-25 and one that hasn't said don't")
    func datesTellBinariesApart() {
        #expect(ConfigVerbs.has(vocabulary: "2026-10-26") && ConfigVerbs.has(vocabulary: "2026-11-02"))
        #expect(!ConfigVerbs.has(vocabulary: "2026-10-25") && !ConfigVerbs.has(vocabulary: nil))
    }
}
