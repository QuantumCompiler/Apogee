import Foundation

/// Where one of the CLI's top-level commands lives in the app (ADR 0003, the
/// parity law): every capability the CLI offers a person has a classified
/// place -- a screen, the console, or a carve-out with its reason.
public struct CommandPlace: Sendable, Equatable {
    public enum Kind: Sendable, Equatable {
        /// A screen of the app, named by the module that draws it.
        case screen(module: String)
        /// The console's: run there as typed (2b).
        case console
        /// A gap, named by the queued item that gives it its screen -- never
        /// "later".
        case queued(item: String)
        /// No place, on purpose: the console refuses it with this reason.
        case carvedOut(reason: String)
        /// A protocol other programs speak, never a person's page.
        case plumbing
    }

    public let command: String
    public let kind: Kind
    /// What the place covers, in a line.
    public let note: String

    public init(_ command: String, _ kind: Kind, _ note: String) {
        self.command = command
        self.kind = kind
        self.note = note
    }
}

/// THE table: every top-level command of `apogee`, today's truth -- the GUI's
/// analog of the CLI's `tui_parity.cpp`. A queued gap names its item, which
/// flips the row to its screen when it ships; a new CLI command fails the
/// app's suite until it is classified here.
public enum ParityTable {
    public static let places: [CommandPlace] = [
        // --- screens: drawn in the app today ------------------------------------
        CommandPlace("chat", .screen(module: "PresentationSession"), "the session window: a conversation driven over machine mode"),
        CommandPlace("chats", .screen(module: "PresentationSessions"), "Sessions: the saved conversations, renamed, deleted, resumed"),
        CommandPlace("models", .screen(module: "PresentationModels"), "Models: the library, a model's details, the roles, downloads"),
        CommandPlace("config", .screen(module: "PresentationSettings"), "Settings' panes, and Suites (PresentationSuites)"),
        CommandPlace("version", .screen(module: "PresentationSettings"), "Settings › General, and the window's subtitle"),
        CommandPlace("tui", .screen(module: "PresentationShell"), "the window itself: the app is the full-screen shell's counterpart"),
        CommandPlace("system", .screen(module: "PresentationMonitor"), "the system monitor overlay"),
        CommandPlace("check", .screen(module: "PresentationSettings"), "Settings › Backends & keys: health, with each failing row's remedy"),
        CommandPlace("providers", .screen(module: "PresentationSettings"), "Settings › Backends & keys: the providers on this machine"),
        CommandPlace("graph", .screen(module: "PresentationGraph"), "Code graph, and a collection's prose graph: its communities, report and decisions"),
        CommandPlace("task", .screen(module: "PresentationTasks"), "Tasks; recipes are 2f's"),
        CommandPlace("auth", .screen(module: "PresentationSettings"), "Settings › Backends & keys: keys added on stdin, kept nowhere"),
        CommandPlace("video", .screen(module: "PresentationVideo"), "Video: clips, and compositions (PresentationCompose)"),
        CommandPlace("voice", .screen(module: "PresentationVoice"), "Voice: speeches, say, playback by the timings"),
        CommandPlace("bootstrap", .screen(module: "PresentationSettings"), "Settings › Backends & keys: install runtimes and register providers"),
        CommandPlace("knowledge", .screen(module: "PresentationKnowledge"), "Knowledge › Records: list, ask, capture, link, move, delete, export"),
        CommandPlace("embed", .screen(module: "PresentationKnowledge"), "Knowledge › Collections: list, health, search, ingest, delete"),
        CommandPlace("train", .screen(module: "PresentationTraining"), "Training › Runs and Regimes: runs launched, gated, promoted, rolled back"),
        CommandPlace("datasets", .screen(module: "PresentationTraining"), "Training › Datasets: create, synthesize, prepare, delete; the kits"),
        // --- gaps: each named by its queued item --------------------------------
        CommandPlace("agents", .queued(item: "2e"), "the agents library"),
        CommandPlace("mcp", .queued(item: "2e"), "the MCP servers and presets"),
        CommandPlace("symphonies", .queued(item: "2e"), "the symphony catalog and shows"),
        CommandPlace("execute", .queued(item: "2f"), "execute sessions: suite, model, project folder, orchestrate"),
        CommandPlace("review", .queued(item: "2g"), "reviews, targets and sweeps, the skills"),
        // --- the console's: run there as typed ----------------------------------
        CommandPlace("analyze", .console, "a one-shot agent workflow over an input"),
        CommandPlace("complete", .console, "a one-shot completion for scripts; a session is the conversation's place"),
        // --- carved out: no place, refused with the reason ----------------------
        CommandPlace("serve", .carvedOut(reason: "a daemon the app would orphan when it quits -- run it at a prompt, or under a service manager"), "a server deployment's daemon"),
        CommandPlace("reset", .carvedOut(reason: "deletes the data directory the running app stands on -- run it at a prompt, with the app closed"), "resets the data directory to a fresh install"),
        CommandPlace("uninstall", .carvedOut(reason: "removes Apogee and the data directory the app drives -- the app never uninstalls the binary it runs; run it at a prompt, with the app closed"), "removes Apogee and its data directory"),
        // --- plumbing: protocols, not pages -------------------------------------
        CommandPlace("__complete", .plumbing, "the completion protocol the console's Tab asks"),
        CommandPlace("__mcp-tools", .plumbing, "the MCP stdio server a client spawns"),
        CommandPlace("__machine-schema", .plumbing, "the machine protocol's JSON Schema, for a driver to read"),
    ]

    /// The place of a command word, if it has one.
    public static func place(of command: String) -> CommandPlace? {
        places.first { $0.command == command }
    }

    /// Why the console refuses a command, when the table carves it out.
    public static func refusal(of command: String) -> String? {
        if case .carvedOut(let reason) = place(of: command)?.kind { return reason }
        return nil
    }

    /// What breaks the law over the binary's own command words: a word with
    /// no place, a classified command the binary no longer has (plumbing
    /// aside -- completion never lists it), a command classified twice, and
    /// an empty word list, which would pass vacuously.
    public static func violations(words: [String]) -> [String] {
        guard !words.isEmpty else { return ["no command words -- the law would pass vacuously"] }
        var found: [String] = []
        for word in words where place(of: word) == nil {
            found.append("'\(word)' has no place in the app -- classify it (ADR 0003)")
        }
        for place in places where place.kind != .plumbing && !words.contains(place.command) {
            found.append("'\(place.command)' is classified, but the binary no longer has it")
        }
        var seen: Set<String> = []
        for place in places where !seen.insert(place.command).inserted {
            found.append("'\(place.command)' is classified twice")
        }
        return found
    }
}
