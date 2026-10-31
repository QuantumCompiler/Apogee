import BusinessMedia
import Foundation
import Testing

/// A person's text is never an option (GUI 4g): a clip's prompt and a
/// composition's brief go after `--`, so the binary reads them as text however
/// they begin -- on the branch binary `video compose "-m cats brief"` took the
/// brief as `-m` and answered "there is nothing to compose", and
/// `video generate "-x prompt"` was "not expected"; after `--` each reaches
/// its run. Every option goes before the `--`.
@Suite("Media text after --")
struct MediaTextAfterDashesTests {
    @Test("a clip's prompt is the last word, after --, whatever it begins with; a clip with no prompt has no --")
    func clipPrompt() {
        var form = ClipForm()
        form.prompt = "-x a fox at dusk"
        form.seed = 7
        form.preset = .draft
        #expect(form.arguments(output: "/m/a.mp4") == ["video", "generate", "-o", "/m/a.mp4", "--seed", "7", "--draft", "--", "-x a fox at dusk"])
        var extend = ClipForm()
        extend.start = .extend("/m/a.mp4")
        extend.prompt = "--final"
        #expect(extend.arguments(output: "/m/b.mp4") == ["video", "generate", "--extend", "/m/a.mp4", "-o", "/m/b.mp4", "--", "--final"])
        var image = ClipForm()
        image.image = "/m/still.png"
        #expect(image.arguments(output: "/m/c.mp4") == ["video", "generate", "-o", "/m/c.mp4", "--image", "/m/still.png"])
    }

    @Test("a composition's brief is the last word, after --, its dry run before it; an edit's instruction stays --edit's value")
    func composeBrief() {
        var form = ComposeForm()
        form.brief = "-m cats brief"
        form.voice = "af_heart"
        #expect(form.arguments(output: "/m/a.mp4") == ["video", "compose", "-o", "/m/a.mp4", "--voice", "af_heart", "--", "-m cats brief"])
        #expect(form.arguments(output: "/m/a.mp4", dryRun: true) == ["video", "compose", "-o", "/m/a.mp4", "--voice", "af_heart", "--dry-run", "--", "-m cats brief"])
        var edit = ComposeForm()
        edit.from = "/m/a.mp4"
        edit.instruction = "-shorter, please"
        #expect(edit.arguments(output: "/m/e.mp4") == ["video", "compose", "--from", "/m/a.mp4", "--edit", "-shorter, please", "-o", "/m/e.mp4"],
                "an option's value is taken whole by the binary, dash or not")
    }
}
