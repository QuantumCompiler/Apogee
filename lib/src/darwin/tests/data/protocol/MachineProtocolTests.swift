import DataProtocol
import Testing

@Suite("MachineProtocol")
struct MachineProtocolTests {
    @Test("the client reads protocol 1 and refuses any other")
    func understandsOnlyItsVersion() {
        #expect(MachineProtocol.version == 1)
        #expect(MachineProtocol.understands(protocolVersion: 1))
        #expect(!MachineProtocol.understands(protocolVersion: 2))
        #expect(!MachineProtocol.understands(protocolVersion: 0))
    }

    @Test("both directions take the stream-json format")
    func streamFormat() {
        #expect(MachineProtocol.streamFormat == "stream-json")
    }
}
