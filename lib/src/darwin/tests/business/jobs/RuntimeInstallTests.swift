import BusinessJobs
import DataProtocol
import Foundation
import InfrastructureProcess
import Testing

/// What `train setup --with video` printed and left on stderr, recorded from
/// the branch's binary over a sandbox whose pip is a stub
/// (tests/business/media/Fixtures/install-video-*): nothing was installed.
private func recorded(_ name: String) -> String {
    let url = URL(fileURLWithPath: #filePath).deletingLastPathComponent()
        .appendingPathComponent("../media/Fixtures/\(name)")
    return (try? String(contentsOf: url, encoding: .utf8)) ?? ""
}

/// The install replayed as `SupervisedJob` says one: its prose lines, then
/// its end with `status` and the stderr it left; or, `interrupted`, its Stop.
private func install(stdout: String, stderr: String = "", status: Int32, interrupted: Bool = false) -> RuntimeInstall {
    var install = RuntimeInstall(arguments: ["train", "setup", "--with", "video"])
    for line in stdout.split(separator: "\n") { install.apply(.said(String(line))) }
    var tail = BoundedTail()
    tail.append(Data(stderr.utf8))
    install.apply(.ended(JobEnd(exit: ChildExit(reason: .exited, status: status), stderrTail: tail, interrupted: interrupted)))
    return install
}

/// A consented runtime install as its card says it (GUI 4f).
@Suite("RuntimeInstall")
struct RuntimeInstallTests {
    @Test("running: the set, the command, the line it is on")
    func running() {
        var install = RuntimeInstall(arguments: ["train", "setup", "--with", "video"])
        #expect(install.phase == .running)
        #expect(install.command == "apogee train setup --with video")
        #expect(install.set == "video")
        #expect(install.headline == "Installing the video set")
        for line in recorded("install-video-done.txt").split(separator: "\n").prefix(2) { install.apply(.said(String(line))) }
        #expect(install.detail?.hasPrefix("installing the 'video' set: torch>=2.6 diffusers>=0.35") == true)
        install.apply(.busy("Collecting torch>=2.6"))
        #expect(install.detail == "Collecting torch>=2.6", "the busy line while pip runs")
        #expect(install.failure == nil)
    }

    @Test("failed: the CLI's own line, then pip's under it")
    func failed() {
        let install = install(
            stdout: recorded("install-video-failed.txt"), stderr: recorded("install-video-failed.err"), status: 1)
        #expect(install.phase == .failed)
        #expect(install.headline == "The video set did not install")
        #expect(install.failure == "apogee train: pip install for the 'video' set failed (exit code 1):\nERROR: Could not install packages: this sandbox's pip is a stub with no network")
        #expect(install.detail == install.failure)
    }

    @Test("installed, and stopped: each said as it ended")
    func ended() {
        let installed = install(stdout: recorded("install-video-done.txt"), status: 0)
        #expect(installed.phase == .installed)
        #expect(installed.headline == "Installed the video set")
        #expect(installed.detail == "python env: at /Users/someone/.apogee/training/venv; sets: video")
        let stopped = install(stdout: "", status: 130, interrupted: true)
        #expect(stopped.phase == .stopped)
        #expect(stopped.headline == "The video set's install was stopped before it finished")
        #expect(stopped.failure == nil)
    }

    @Test("a failure with no line of the CLI's own is said by its last word")
    func failedWithoutItsOwnLine() {
        let install = install(stdout: "", stderr: "Killed: 9\n", status: 137)
        #expect(install.failure == "Killed: 9")
    }
}
