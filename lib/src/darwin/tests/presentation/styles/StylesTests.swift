import AppKit
import PresentationStyles
import Testing

@Suite("Styles", .serialized)
struct StylesTests {
    @Test("every color token has a unique design name and 24-bit values")
    func tokensAreWellFormed() {
        let names = Palette.tokens.map(\.name)
        #expect(Set(names).count == names.count)
        for token in Palette.tokens {
            #expect(token.light <= 0xFFFFFF, "\(token.name) light")
            #expect(token.dark <= 0xFFFFFF, "\(token.name) dark")
            #expect(token.contrast <= 0xFFFFFF, "\(token.name) contrast")
            #expect(token.light != token.dark, "\(token.name) is the same in both appearances")
        }
    }

    @Test("the tokens are the design's Graphite values")
    func tokensMatchTheDesign() {
        #expect(Palette.windowToken == ColorToken("surface.window", light: 0xF7F7F5, dark: 0x131415, contrast: 0x000000))
        #expect(Palette.accentToken == ColorToken("status.accent", light: 0x0B7A68, dark: 0x7FD4C1, contrast: 0x5EF0D2))
        #expect(Palette.errorToken == ColorToken("status.error", light: 0xB3261E, dark: 0xF2836F, contrast: 0xFF8A7A))
        #expect(Palette.sidebarToken == ColorToken("surface.sidebar", light: 0xEEEEEA, dark: 0x0E0F10, contrast: 0x0A0A0A))
        #expect(Palette.selectionToken == ColorToken("surface.selection", light: 0xE1E4E1, dark: 0x25282B, contrast: 0x2A2A2A))
        #expect(Palette.apiToken == ColorToken("model.api", light: 0x9F5600, dark: 0xE9A23B, contrast: 0xFFC04D))
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

    @Test("the theme switch: the person's theme picks the value, following the system picks by appearance; the text size scales every step")
    func themes() {
        defer { Appearance.apply(theme: .system, scale: 1) }
        let token = Palette.textPrimaryToken
        Appearance.apply(theme: .system, scale: 1)
        #expect(token.value(darkAppearance: false) == token.light && token.value(darkAppearance: true) == token.dark)
        #expect(Appearance.colorScheme == nil)
        Appearance.apply(theme: .light, scale: 1)
        #expect(token.value(darkAppearance: true) == token.light)
        Appearance.apply(theme: .dark, scale: 1)
        #expect(token.value(darkAppearance: false) == token.dark)
        Appearance.apply(theme: .contrast, scale: 1)
        #expect(token.value(darkAppearance: false) == 0xFFFFFF)
        #expect(Appearance.colorScheme == .dark)
        Appearance.apply(theme: .system, scale: 14.0 / 13.0)
        #expect(TypeScale.scaled(TypeScale.body) == 14)
        #expect(TypeScale.scaled(TypeScale.meta) == 12, "11 x 14/13 is 11.85, to the half point 12")
        Appearance.apply(theme: .system, scale: 12.0 / 13.0)
        #expect(TypeScale.scaled(TypeScale.title) == 12.5)
    }
}
