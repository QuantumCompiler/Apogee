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

/// A command the console refuses (ADR 0003), by the words it starts with --
/// a top-level command, or a command and its verb -- and the reason it says.
public struct Refusal: Sendable, Equatable {
    /// The command's words: `["reset"]`, `["agents", "edit"]`.
    public let words: [String]
    public let reason: String

    public init(_ words: String, _ reason: String) {
        self.words = words.split(separator: " ").map(String.init)
        self.reason = reason
    }
}

/// THE table: every top-level command of `apogee`, today's truth -- the GUI's
/// analog of the CLI's `tui_parity.cpp`. A queued gap names its item, which
/// flips the row to its screen when it ships; a new CLI command fails the
/// app's suite until it is classified here. It is also the console's one
/// source of refusals, never a second list.
public enum ParityTable {
    public static let places: [CommandPlace] = [
        // --- screens: drawn in the app today ------------------------------------
        CommandPlace("chat", .screen(module: "PresentationSession"), "the session window: a conversation driven over machine mode, under a suite or none -- --suite at launch, and /suite or Suites' Switch the session to it relaunching it with --resume and --suite, as machine mode carries no suite"),
        CommandPlace("chats", .screen(module: "PresentationSessions"), "Sessions: the saved conversations, renamed, deleted, resumed, their details (`chats info`), compacted when asked (`chats compact`), and what recall holds with Forget beside it (`chats recall`) -- and `chats show`, a resumed one's earlier turns drawn in the session window (PresentationSession)"),
        CommandPlace("execute", .screen(module: "PresentationSessions"), "Sessions' execute door: a suite or bare, a project folder, orchestrate; /play in the session"),
        CommandPlace("models", .screen(module: "PresentationModels"), "Models: the library, a model's details, the roles, downloads; and Suites' fit, each suite's footprint as models status --suite states it (PresentationSuites)"),
        CommandPlace("config", .screen(module: "PresentationSettings"), "Settings' panes -- a vendor backend's server_tools consent shown beside it, read by config get -- Suites (PresentationSuites), a backend's adapters listed with their pins and edited by config set-backend --adapters (PresentationModels), and an MCP server removed by config delete-mcp-server (PresentationLibrary)"),
        CommandPlace("version", .screen(module: "PresentationSettings"), "Settings › General, and the window's subtitle"),
        CommandPlace("tui", .screen(module: "PresentationShell"), "the window itself: the app is the full-screen shell's counterpart"),
        CommandPlace("system", .screen(module: "PresentationMonitor"), "the system monitor overlay"),
        CommandPlace("check", .screen(module: "PresentationSettings"), "Settings › Backends & keys: health, with each failing row's remedy"),
        CommandPlace("providers", .screen(module: "PresentationSettings"), "Settings › Backends & keys: the providers on this machine, one scanned provider registered from its row as the TUI's r writes it (config add-backend, asked first)"),
        CommandPlace("graph", .screen(module: "PresentationGraph"), "Code graph, and a collection's prose graph: its communities, report and decisions; exported to a file the person picks (graph export html|graphml|mermaid), its report saved the same way (graph report --out), deduped dry run first (graph dedupe) and deleted (graph delete), each asked"),
        CommandPlace("task", .screen(module: "PresentationTasks"), "Tasks, recipes in the new-task sheet"),
        CommandPlace("auth", .screen(module: "PresentationSettings"), "Settings › Backends & keys: keys added on stdin, kept nowhere"),
        CommandPlace("video", .screen(module: "PresentationVideo"), "Video: clips, and compositions (PresentationCompose)"),
        CommandPlace("voice", .screen(module: "PresentationVoice"), "Voice: speeches, say, playback by the timings"),
        CommandPlace("bootstrap", .screen(module: "PresentationSettings"), "Settings › Backends & keys: install runtimes and register providers"),
        CommandPlace("knowledge", .screen(module: "PresentationKnowledge"), "Knowledge › Records: list, ask, capture, link, move, delete, export, and a record's raw conversation as knowledge info --raw prints it"),
        CommandPlace("embed", .screen(module: "PresentationKnowledge"), "Knowledge › Collections: list, health, search, ingest, delete"),
        CommandPlace("train", .screen(module: "PresentationTraining"), "Training › Runs, Pipelines, Cycle and Regimes: runs launched, gated, promoted -- merged, or the adapter alone (--adapter, gated on a quantized base by --suite and --judge) -- and rolled back; the named pipelines listed with their stage plans (train pipeline list|show), run, resumed and watched stage by stage (train pipeline run|resume|status); the cycle's state, one pass run, halted and resumed (train cycle run|status|halt|resume) -- its schedule the person's own, launchd or cron"),
        CommandPlace("datasets", .screen(module: "PresentationTraining"), "Training › Datasets: create, pull from Hugging Face, synthesize, prepare, delete; the kits"),
        CommandPlace("agents", .screen(module: "PresentationLibrary"), "Library › Agents: list, create, delete, the person's own files edited in place"),
        CommandPlace("mcp", .screen(module: "PresentationLibrary"), "Library › MCP servers: connect state, presets registered, enable, disable, a tool tried, a server removed (config delete-mcp-server, asked first)"),
        CommandPlace("symphonies", .screen(module: "PresentationLibrary"), "Library › Symphonies: the catalog, a play supervised, own spec files edited and checked"),
        CommandPlace("review", .screen(module: "PresentationReview"), "Review, ⇧⌘R: a merge request dry run first, posted on consent; targets and sweeps; the skills"),
        // --- gaps: each named by its queued item (none since 2g) ------------------
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
        CommandPlace("__machine-schema", .plumbing, "the machine protocol's JSON Schema: read by the app at launch, and again when the binary changes, for what the binary can do -- never a page"),
    ]

    /// Every command the console refuses, by its words -- the set the CLI's
    /// exec line refuses (`tui_parity.cpp`'s `kRefusals`, ADR 0010), each in
    /// the app's own words (GUI 4g):
    /// - the doors the app already is (`chat`, `execute`, `tui`): a child on
    ///   the console's closed stdin would read nothing and end, so each names
    ///   where the app opens it;
    /// - the `$EDITOR` verbs, which need a terminal the console has none of;
    /// - the carve-outs, by their rows' reasons.
    public static let refusals: [Refusal] = [
        // The doors: the app already is what they open.
        Refusal("chat", "a conversation is a session in the app -- start one with New session (⌘N), or reopen one from Sessions"),
        Refusal("execute", "an execute session is a session in the app -- open one with New execute session (⌥⌘N)"),
        Refusal("tui", "the app is the full-screen shell's counterpart already -- run it at a prompt for the terminal's own"),
        // The `$EDITOR` wall: the editor needs a terminal.
        Refusal("agents edit", "opens your $EDITOR, which needs a terminal the console doesn't have -- run it at a prompt"),
        Refusal("symphonies edit", "opens your $EDITOR, which needs a terminal the console doesn't have -- run it at a prompt"),
    ] + places.compactMap { place -> Refusal? in
        // The carve-outs: each refused by its row's reason.
        if case .carvedOut(let reason) = place.kind { Refusal(place.command, reason) } else { nil }
    }

    /// The place of a command word, if it has one.
    public static func place(of command: String) -> CommandPlace? {
        places.first { $0.command == command }
    }

    /// The refusal for a command path -- the command's own words, past any
    /// root option, as the binary reads them -- when it starts with refused
    /// words: `["agents", "edit", "reviewer"]` is refused as `agents edit`,
    /// `["agents", "list"]` is not.
    public static func refusal(of path: some Collection<String>) -> Refusal? {
        refusals.first { path.starts(with: $0.words) }
    }

    /// What breaks the law over the binary's own command words: a word with
    /// no place, a classified command the binary no longer has (plumbing
    /// aside -- completion never lists it), a command classified twice, a
    /// refusal of a command the binary no longer has or with no reason, and
    /// an empty word list, which would pass vacuously.
    public static func violations(words: [String]) -> [String] {
        violations(words: words, refusals: refusals)
    }

    /// The same law over `refusals` in place of the table's: how a planted
    /// refusal is shown to be named.
    static func violations(words: [String], refusals: [Refusal]) -> [String] {
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
        for refusal in refusals {
            let said = refusal.words.joined(separator: " ")
            if refusal.words.first.map(words.contains) != true {
                found.append("the console refuses '\(said)', which the binary no longer has")
            }
            if refusal.reason.isEmpty {
                found.append("the console refuses '\(said)' with no reason")
            }
        }
        return found
    }
}
