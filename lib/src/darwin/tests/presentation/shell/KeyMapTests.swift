import Testing

@testable import PresentationShell

@Suite("KeyMap")
struct KeyMapTests {
    @Test("the places are the design's seven, in its order, on ⌘1 to ⌘7")
    func places() {
        #expect(Destination.allCases.map(\.title) == ["Sessions", "Models", "Suites", "Tasks", "Code graph", "Video", "Voice", "Knowledge", "Training", "Library"])
        #expect(Destination.allCases.map(\.key.symbols) == ["⌘1", "⌘2", "⌘3", "⌘4", "⌘5", "⌘6", "⌘7", "⌘8", "⌘9", "⌘0"])
        for destination in Destination.allCases {
            #expect(KeyMap.chord(for: .go(destination)) == destination.key)
        }
    }

    @Test("every place and overlay the shell owns has a key, and no two share one")
    func everyCommandHasOneKey() {
        let chords = KeyMap.shell.map(\.chord)
        #expect(Set(chords).count == chords.count)
        #expect(KeyMap.chord(for: .modelsLibrary)?.symbols == "⇧⌘L")
        #expect(KeyMap.chord(for: .toggleMonitor)?.symbols == "⌥⌘M")
        #expect(KeyMap.chord(for: .toggleSidebar)?.symbols == "⌃⌘S")
        #expect(KeyMap.chord(for: .openSettings)?.symbols == "⌘,")
    }

    @Test("each of the shell's keys is the one the design's keyboard list gives")
    func keysMatchTheDesignList() {
        let listed = Set(KeyMap.reference.map(\.keys))
        for binding in KeyMap.shell where !binding.isPlace {
            #expect(listed.contains(binding.chord.symbols), "\(binding.label) on \(binding.chord.symbols)")
        }
        #expect(KeyMap.reference.contains(KeyReference("Sessions, Models, Suites, Tasks, Code graph, Video, Voice, Knowledge, Training, Library", "⌘1 – ⌘9, ⌘0")))
        #expect(KeyMap.reference.count == 21)
    }

    @Test("chords are written as the Mac's menus write them")
    func symbols() {
        #expect(KeyChord("n", [.shift, .command]).symbols == "⇧⌘N")
        #expect(KeyChord("i", [.command, .option]).symbols == "⌥⌘I")
        #expect(KeyChord("s", [.control, .command]).symbols == "⌃⌘S")
        #expect(KeyChord("x", [.control, .option, .shift, .command]).symbols == "⌃⌥⇧⌘X")
    }

    @Test("every unbuilt place is an honest placeholder: what will be there, and today's command")
    func placeholders() {
        #expect(Destination.sessions.placeholder == nil)
        #expect(Destination.models.placeholder == nil)
        for destination in Destination.allCases where destination.placeholder != nil {
            let placeholder = try! #require(destination.placeholder)
            #expect(placeholder.title == "Not built yet")
            #expect(placeholder.about.hasSuffix("will be here."))
            #expect(placeholder.command.hasPrefix("apogee "))
        }
    }
}
