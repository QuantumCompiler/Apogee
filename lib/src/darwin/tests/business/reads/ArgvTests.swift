import BusinessReads
import Testing

/// The argv rule (GUI 4g): a person's text after `--`, every option before it.
@Suite("Argv")
struct ArgvTests {
    @Test("text goes after the end of options; an option added later lands before it, or at the end when there is none")
    func textAndOptions() {
        let line = ["voice", "say", "-o", "/m/a.wav"] + Argv.text("--draft")
        #expect(line == ["voice", "say", "-o", "/m/a.wav", "--", "--draft"])
        #expect(Argv.adding(["--output-format", "stream-json"], to: line)
            == ["voice", "say", "-o", "/m/a.wav", "--output-format", "stream-json", "--", "--draft"])
        #expect(Argv.adding(["--dry-run"], to: ["chats", "list"]) == ["chats", "list", "--dry-run"])
        #expect(["chats", "title", "c1"] + Argv.text("--", "x") == ["chats", "title", "c1", "--", "--", "x"], "a text that is itself -- stays text")
        #expect(Argv.adding(["--json"], to: ["chats", "title", "c1", "--", "--", "x"]) == ["chats", "title", "c1", "--json", "--", "--", "x"],
                "added before the first --, the end of options")
    }

    @Test("the options are the words before the end of options: a text reading like a flag is not one")
    func options() {
        let line = ["video", "compose", "-o", "/m/a.mp4", "--", "--dry-run"]
        #expect(!Argv.options(of: line).contains("--dry-run"))
        #expect(Argv.options(of: Argv.adding(["--dry-run"], to: line)).contains("--dry-run"))
        #expect(Array(Argv.options(of: ["check", "--fix"])) == ["check", "--fix"])
    }
}
