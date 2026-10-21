import Foundation
import InfrastructureProcess
import Testing

@Suite("ProcessMemory")
struct ProcessMemoryTests {
    @Test("a running process's resident bytes are read; a gone one's are none")
    func resident() throws {
        let own = try #require(ProcessMemory.resident(pid: getpid()))
        #expect(own > 1_000_000, "this test process holds more than a megabyte")
        #expect(ProcessMemory.resident(pid: 999_999) == nil)
    }
}
