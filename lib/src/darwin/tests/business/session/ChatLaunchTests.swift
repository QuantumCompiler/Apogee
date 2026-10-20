import BusinessSession
import Foundation
import InfrastructureProcess
import Testing

@Suite("ChatLaunch")
struct ChatLaunchTests {
    let binary = URL(fileURLWithPath: "/Users/someone/.local/bin/apogee")

    @Test("a chat with tools: both directions stream-json, then --tools")
    func withTools() {
        let command = ChatLaunch(tools: true).command(binary: binary)
        #expect(command.executable == binary)
        #expect(
            command.arguments == [
                "chat", "--output-format", "stream-json", "--input-format", "stream-json", "--tools",
            ])
    }

    @Test("tools are on unless asked otherwise, matching the CLI's interactive default")
    func toolsByDefault() {
        #expect(ChatLaunch().tools)
        #expect(ChatLaunch(tools: false).arguments == [
            "chat", "--output-format", "stream-json", "--input-format", "stream-json",
        ])
    }
}
