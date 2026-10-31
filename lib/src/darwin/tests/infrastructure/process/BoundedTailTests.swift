import Foundation
import InfrastructureProcess
import Testing

@Suite("BoundedTail")
struct BoundedTailTests {
    @Test("only the last bytes are kept")
    func bounded() {
        var tail = BoundedTail(limit: 8)
        tail.append(Data("0123456789".utf8))
        tail.append(Data("ab".utf8))
        #expect(tail.text == "456789ab")
    }

    @Test("the last line with anything on it is what a failure says")
    func lastLine() {
        var tail = BoundedTail()
        #expect(tail.lastLine == nil)
        #expect(tail.isEmpty)
        tail.append(Data("loading\nfatal: the model went away  \n\n".utf8))
        #expect(tail.lastLine == "fatal: the model went away")
    }
}
