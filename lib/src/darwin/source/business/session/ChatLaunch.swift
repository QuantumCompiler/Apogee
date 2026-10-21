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

    public init(tools: Bool = true, resume: String? = nil) {
        self.tools = tools
        self.resume = resume
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
        return arguments
    }

    /// The child to spawn for `binary`, an `apogee` executable.
    public func command(binary: URL) -> ChildCommand {
        ChildCommand(executable: binary, arguments: arguments)
    }
}
