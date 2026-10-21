import DataProtocol
import Foundation
import InfrastructureProcess

/// How a driven chat session's child is started: `apogee chat` with both
/// directions in stream-json, so one child serves the whole conversation
/// (machine-mode.md -> The two flags).
public struct ChatLaunch: Sendable, Equatable {
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

    public init(
        tools: Bool = true, resume: String? = nil, settings: RunSettings? = nil, privately: Bool = false,
        noRecall: Bool = false
    ) {
        self.tools = tools
        self.resume = resume
        // `nil` rather than a default naming DataProtocol's type: a default
        // argument is compiled into its caller (CLAUDE.md -> Code Style).
        self.settings = settings ?? RunSettings()
        self.privately = privately
        self.noRecall = noRecall
    }

    /// The arguments after `apogee`.
    public var arguments: [String] {
        var arguments = [
            "chat",
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
        return arguments
    }

    /// The child to spawn for `binary`, an `apogee` executable.
    public func command(binary: URL) -> ChildCommand {
        ChildCommand(executable: binary, arguments: arguments)
    }
}
