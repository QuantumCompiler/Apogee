import DataProtocol
import Testing

@Suite("JSONValue")
struct JSONValueTests {
    @Test("a JSON text is kept as it was: booleans as booleans, numbers as numbers")
    func parse() {
        let value = JSONValue.parse(#"{"a":true,"b":1,"c":1.5,"d":"x","e":[null],"f":{}}"#)
        #expect(value?["a"] == .bool(true))
        #expect(value?["b"]?.intValue == 1)
        #expect(value?["c"] == .number(1.5))
        #expect(value?["c"]?.intValue == nil)
        #expect(value?["d"]?.stringValue == "x")
        #expect(value?["e"] == .array([.null]))
        #expect(value?["f"] == .object([:]))
        #expect(JSONValue.parse("not json") == nil)
    }

    @Test("a value serializes to one line, keys sorted")
    func serialized() {
        let value = JSONValue.parse(#"{"b":[1,true,"x/y"],"a":{"z":null}}"#)
        #expect(value?.serialized == #"{"a":{"z":null},"b":[1,true,"x/y"]}"#)
    }
}
