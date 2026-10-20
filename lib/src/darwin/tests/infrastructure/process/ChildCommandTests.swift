import Foundation
import InfrastructureProcess
import Testing

@Suite("ChildCommand")
struct ChildCommandTests {
    @Test("its variables are laid over the parent's environment")
    func environmentOverParent() {
        let command = ChildCommand(
            executable: URL(fileURLWithPath: "/usr/bin/true"),
            environment: ["APOGEE_HOME": "/tmp/sandbox", "LANG": "C"])
        let merged = command.environment(over: ["PATH": "/usr/bin", "LANG": "en_US.UTF-8"])
        #expect(merged == ["PATH": "/usr/bin", "LANG": "C", "APOGEE_HOME": "/tmp/sandbox"])
    }

    @Test("with nothing set, the child inherits the parent's environment as it is")
    func emptyEnvironmentInherits() {
        let command = ChildCommand(executable: URL(fileURLWithPath: "/usr/bin/true"))
        #expect(command.environment(over: ["PATH": "/usr/bin"]) == ["PATH": "/usr/bin"])
        #expect(command.arguments.isEmpty)
        #expect(command.workingDirectory == nil)
    }
}
