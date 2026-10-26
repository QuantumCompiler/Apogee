import DataProtocol
import Foundation
import InfrastructureProcess

/// How a driven chat session's child is started: `apogee chat` with both
/// directions in stream-json, so one child serves the whole conversation
/// (machine-mode.md -> The two flags) -- or `apogee execute`, the same
/// session's other opening, with the same lines in and events out.
public struct ChatLaunch: Sendable, Equatable {
    /// `execute`'s door (the CLI's 27s and 41a-41c): under a suite, its
    /// symphonies playable with `/play`, or bare on one model -- opened in
    /// the folder the session works in, which as a project brings its
    /// `AGENTS.md` and the coder persona, said by the session's own notices.
    public struct Execute: Sendable, Equatable {
        /// `--suite <name>`; `nil` leaves it to the config -- the default
        /// suite, else bare on the model (`execute` refuses `--suite off`).
        public var suite: String?
        /// `--orchestrate`: the suite's chat model may play its symphonies
        /// as tools. A suite's -- the CLI refuses it bare.
        public var orchestrate: Bool

        public init(suite: String? = nil, orchestrate: Bool = false) {
            self.suite = suite
            self.orchestrate = orchestrate
        }
    }

    /// Whether the model may call tools -- `--tools`, the CLI's interactive
    /// default. With tools on, the session's asks reach the driver as
    /// `question` events.
    public var tools: Bool
    /// The saved conversation to carry on -- `--resume <id>` -- or `nil` for
    /// a new one.
    public var resume: String?
    /// The run settings to start with: `-m`, `-s`, `--think`, `--effort`,
    /// `-t` -- the flags `chat` and `execute` take for what the `set` line
    /// moves after. A relaunch carries the session's current ones.
    public var settings: RunSettings
    /// A chat that writes nothing (`--private`, the CLI's 52d) -- never with
    /// `resume`, which the CLI refuses.
    public var privately: Bool
    /// Recall no earlier chats (`--no-recall`): a private session's fallback
    /// on a build before `--private`.
    public var noRecall: Bool
    /// `execute` rather than `chat`, as its door says.
    public var execute: Execute?
    /// A chat's suite (the CLI's 27d): `--suite <name>`, or `--suite off`
    /// (`noSuite`) for none; `nil` leaves it to the CLI -- a new chat runs
    /// under `models.default_suite`, a resumed one under the suite it last
    /// had. An `execute` session's is its door's (`Execute.suite`).
    public var suite: String?
    /// `--force`: run the suite even when the CLI prices it over this
    /// machine's memory, which it otherwise refuses at launch (27e).
    public var forceSuite: Bool
    /// The folder the session works in; the client's own default (the home
    /// folder) when `nil`. An `execute` opened in a project reads it as one.
    public var folder: URL?

    public init(
        tools: Bool = true, resume: String? = nil, settings: RunSettings? = nil, privately: Bool = false,
        noRecall: Bool = false, execute: Execute? = nil, folder: URL? = nil, suite: String? = nil,
        forceSuite: Bool = false
    ) {
        self.tools = tools
        self.resume = resume
        // `nil` rather than a default naming DataProtocol's type: a default
        // argument is compiled into its caller (CLAUDE.md -> Code Style).
        self.settings = settings ?? RunSettings()
        self.privately = privately
        self.noRecall = noRecall
        self.execute = execute
        self.folder = folder
        self.suite = suite
        self.forceSuite = forceSuite
    }

    /// The word `--suite` takes for none (`chat --suite off`); `execute`
    /// refuses it, since it opens under a suite or bare.
    public static let noSuite = "off"

    /// The suite the launch names, whichever opening it is: a chat's
    /// `suite`, or the execute door's.
    public var namedSuite: String? {
        execute.map(\.suite) ?? suite
    }

    /// Names `suite` for the next launch -- a chat's `--suite`, or the
    /// execute door's -- forced or not.
    public mutating func name(suite: String?, force: Bool) {
        if execute != nil {
            execute?.suite = suite
        } else {
            self.suite = suite
        }
        forceSuite = force
    }

    /// The arguments after `apogee`.
    public var arguments: [String] {
        var arguments = [
            execute == nil ? "chat" : "execute",
            "--output-format", MachineProtocol.streamFormat,
            "--input-format", MachineProtocol.streamFormat,
        ]
        if tools {
            arguments.append("--tools")
        }
        if let resume {
            arguments += ["--resume", resume]
        }
        if let model = settings.model { arguments += ["-m", model] }
        if let system = settings.system { arguments += ["-s", system] }
        if let think = settings.think { arguments += ["--think", think] }
        if let effort = settings.effort, effort != "auto" { arguments += ["--effort", effort] }
        // As the `set` line writes it: 0.2, and 1 for 1.0.
        if let temperature = settings.temperature { arguments += ["-t", JSONValue.number(temperature).serialized] }
        if privately { arguments.append("--private") }
        if noRecall { arguments.append("--no-recall") }
        if let execute {
            if let suite = execute.suite { arguments += ["--suite", suite] }
            if execute.orchestrate { arguments.append("--orchestrate") }
        } else if let suite {
            arguments += ["--suite", suite]
        }
        if forceSuite { arguments.append("--force") }
        return arguments
    }

    /// The child to spawn for `binary`, an `apogee` executable.
    public func command(binary: URL) -> ChildCommand {
        ChildCommand(executable: binary, arguments: arguments, workingDirectory: folder)
    }
}
