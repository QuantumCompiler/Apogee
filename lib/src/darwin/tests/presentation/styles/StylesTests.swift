import AppKit
import PresentationStyles
import Testing

@Suite("Styles")
struct StylesTests {
    @Test("every color token has a unique design name and 24-bit values")
    func tokensAreWellFormed() {
        let names = Palette.tokens.map(\.name)
        #expect(Set(names).count == names.count)
        for token in Palette.tokens {
            #expect(token.light <= 0xFFFFFF, "\(token.name) light")
            #expect(token.dark <= 0xFFFFFF, "\(token.name) dark")
            #expect(token.light != token.dark, "\(token.name) is the same in both appearances")
        }
    }

    @Test("the tokens are the design's Graphite values")
    func tokensMatchTheDesign() {
        #expect(Palette.windowToken == ColorToken("surface.window", light: 0xF7F7F5, dark: 0x131415))
        #expect(Palette.accentToken == ColorToken("status.accent", light: 0x0B7A68, dark: 0x7FD4C1))
        #expect(Palette.errorToken == ColorToken("status.error", light: 0xB3261E, dark: 0xF2836F))
        #expect(Palette.sidebarToken == ColorToken("surface.sidebar", light: 0xEEEEEA, dark: 0x0E0F10))
        #expect(Palette.selectionToken == ColorToken("surface.selection", light: 0xE1E4E1, dark: 0x25282B))
        #expect(Palette.apiToken == ColorToken("model.api", light: 0x9F5600, dark: 0xE9A23B))
    }

    @Test("the type scale descends, and the spacing steps ascend")
    func scalesAreOrdered() {
        #expect(TypeScale.steps == TypeScale.steps.sorted(by: >))
        let spacing = [Spacing.xxs, Spacing.xs, Spacing.s, Spacing.m, Spacing.l, Spacing.xl, Spacing.xxl]
        #expect(spacing == spacing.sorted())
        #expect(Layout.windowMinWidth > Layout.conversationMax)
        #expect(Layout.sidebar > Layout.sidebarCollapsed)
    }

    @Test("the media player's framework is loaded wherever styles is: AVKit's view exists at run time")
    func playerFrameworkLoaded() {
        #expect(NSClassFromString("AVPlayerView") != nil)
    }
}
