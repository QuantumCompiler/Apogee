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
    /// The run settings to start with: `-m`, `-s`, `--think`, `--effort`.
    /// (Temperature has no launch flag; only the `set` line moves it.)
    public var settings: RunSettings
    /// A chat that writes nothing (`--private`, the CLI's 52d) -- never with
    /// `resume`, which the CLI refuses.
    public var privately: Bool
    /// Recall no earlier chats (`--no-recall`): a private session's fallback
    /// on a build before `--private`.
    public var noRecall: Bool
    /// `execute` rather than `chat`, as its door says.
    public var execute: Execute?
    /// The folder the session works in; the client's own default (the home
    /// folder) when `nil`. An `execute` opened in a project reads it as one.
    public var folder: URL?

    public init(
        tools: Bool = true, resume: String? = nil, settings: RunSettings? = nil, privately: Bool = false,
        noRecall: Bool = false, execute: Execute? = nil, folder: URL? = nil
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
        if privately { arguments.append("--private") }
        if noRecall { arguments.append("--no-recall") }
        if let execute {
            if let suite = execute.suite { arguments += ["--suite", suite] }
            if execute.orchestrate { arguments.append("--orchestrate") }
        }
        return arguments
    }

    /// The child to spawn for `binary`, an `apogee` executable.
    public func command(binary: URL) -> ChildCommand {
        ChildCommand(executable: binary, arguments: arguments, workingDirectory: folder)
    }
}
