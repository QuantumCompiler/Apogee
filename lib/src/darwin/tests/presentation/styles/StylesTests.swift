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

    @Test("the five code tokens are the design's, in light, dark and high contrast, and in the token list")
    func codeTokensMatchTheDesign() {
        let code = [
            ColorToken("code.keyword", light: 0x7A3FC0, dark: 0xC3A6F0, contrast: 0xD8B9FF),
            ColorToken("code.string", light: 0x2F7A1F, dark: 0xA5D68E, contrast: 0xB8F0A0),
            ColorToken("code.comment", light: 0x67686A, dark: 0x8C887F, contrast: 0xB5B5B5),
            ColorToken("code.number", light: 0x8F5A00, dark: 0xE5B567, contrast: 0xFFC04D),
            ColorToken("code.function", light: 0x1F5FA8, dark: 0x8EC3F0, contrast: 0x9ED4FF),
        ]
        #expect([Palette.codeKeywordToken, Palette.codeStringToken, Palette.codeCommentToken, Palette.codeNumberToken, Palette.codeFunctionToken] == code)
        for token in code {
            #expect(Palette.tokens.contains(token), "\(token.name)")
        }
    }

    @Test("every code token, and each colour a diff draws, reads at 4.5:1 or more on surface.code in light, dark and high contrast (WCAG's ratio, computed)")
    func codeContrast() {
        /// WCAG 2's relative luminance of 0xRRGGBB.
        func luminance(_ rgb: UInt32) -> Double {
            func linear(_ channel: UInt32) -> Double {
                let value = Double(channel) / 255
                return value <= 0.04045 ? value / 12.92 : pow((value + 0.055) / 1.055, 2.4)
            }
            return 0.2126 * linear(rgb >> 16 & 0xFF) + 0.7152 * linear(rgb >> 8 & 0xFF) + 0.0722 * linear(rgb & 0xFF)
        }
        func ratio(_ a: UInt32, _ b: UInt32) -> Double {
            let (x, y) = (luminance(a), luminance(b))
            return (max(x, y) + 0.05) / (min(x, y) + 0.05)
        }
        let ground = Palette.codeToken
        let drawn = [
            Palette.codeKeywordToken, Palette.codeStringToken, Palette.codeCommentToken, Palette.codeNumberToken, Palette.codeFunctionToken,
            Palette.accentToken, Palette.errorToken, Palette.textTertiaryToken,
        ]
        for token in drawn {
            #expect(ratio(token.light, ground.light) >= 4.5, "\(token.name) in light: \(ratio(token.light, ground.light))")
            #expect(ratio(token.dark, ground.dark) >= 4.5, "\(token.name) in dark: \(ratio(token.dark, ground.dark))")
            #expect(ratio(token.contrast, ground.contrast) >= 4.5, "\(token.name) in high contrast: \(ratio(token.contrast, ground.contrast))")
        }
        // The lowest, as the item measured it: code.string in light, 4.81:1.
        #expect((ratio(Palette.codeStringToken.light, ground.light) * 100).rounded() == 481)
        #expect(ratio(0xFFFFFF, 0x000000) == 21, "the formula's own end: white on black")
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
