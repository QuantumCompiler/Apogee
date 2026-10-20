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

    public init(tools: Bool = true) {
        self.tools = tools
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
        return arguments
    }

    /// The child to spawn for `binary`, an `apogee` executable.
    public func command(binary: URL) -> ChildCommand {
        ChildCommand(executable: binary, arguments: arguments)
    }
}
