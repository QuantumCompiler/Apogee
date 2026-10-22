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

    @Test("the shell's PATH is laid under its variables: the parent's replaced, its own kept")
    func pathUnderItsVariables() {
        let parent = ["PATH": "/usr/bin:/bin:/usr/sbin:/sbin", "HOME": "/Users/someone"]
        let plain = ChildCommand(executable: URL(fileURLWithPath: "/usr/bin/true"), environment: ["APOGEE_HOME": "/tmp/sandbox"])
        #expect(
            plain.environment(over: parent, path: "/Users/someone/.local/bin:/usr/bin")
                == ["PATH": "/Users/someone/.local/bin:/usr/bin", "HOME": "/Users/someone", "APOGEE_HOME": "/tmp/sandbox"])
        let ownPath = ChildCommand(executable: URL(fileURLWithPath: "/usr/bin/true"), environment: ["PATH": "/only/mine"])
        #expect(ownPath.environment(over: parent, path: "/Users/someone/.local/bin:/usr/bin")["PATH"] == "/only/mine")
    }

    @Test("with nothing set, the child inherits the parent's environment as it is")
    func emptyEnvironmentInherits() {
        let command = ChildCommand(executable: URL(fileURLWithPath: "/usr/bin/true"))
        #expect(command.environment(over: ["PATH": "/usr/bin"]) == ["PATH": "/usr/bin"])
        #expect(command.arguments.isEmpty)
        #expect(command.workingDirectory == nil)
    }
}
