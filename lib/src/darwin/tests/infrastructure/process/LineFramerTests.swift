import Foundation
import InfrastructureProcess
import Testing

@Suite("LineFramer")
struct LineFramerTests {
    @Test("a line cut across reads comes out whole, and several in one read come out apart")
    func framing() {
        var framer = LineFramer()
        #expect(framer.push(Data("{\"type\":\"ses".utf8)) == [])
        #expect(framer.push(Data("sion\"}\n{\"a\":1}\n{\"b\"".utf8)) == ["{\"type\":\"session\"}", "{\"a\":1}"])
        #expect(framer.push(Data(":2}\n".utf8)) == ["{\"b\":2}"])
        #expect(framer.finish() == nil)
    }

    @Test("a character split across reads is decoded whole, and a CRLF ending loses its CR")
    func utf8AndCRLF() {
        var framer = LineFramer()
        let bytes = Array("café\r\n".utf8)
        #expect(framer.push(Data(bytes[0..<4])) == [])
        #expect(framer.push(Data(bytes[4...])) == ["café"])
    }

    @Test("bytes that are not UTF-8 become U+FFFD, and a last line without its newline is kept")
    func mendingAndTheLastLine() {
        var framer = LineFramer()
        #expect(framer.push(Data([0x61, 0xFF, 0x62, 0x0A])) == ["a\u{FFFD}b"])
        #expect(framer.push(Data("tail".utf8)) == [])
        #expect(framer.finish() == "tail")
        #expect(framer.finish() == nil)
    }
}
