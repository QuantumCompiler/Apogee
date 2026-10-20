import AppKit
import SwiftUI

/// One color token: its value in the light appearance and in the dark one.
///
/// The values are the design's Graphite Light and Graphite Dark themes
/// (lib/design/spec/json/tokens.json), copied rather than read: the design is
/// a reference, never an input to the build. The app follows the system's
/// appearance -- there is no theme setting of its own.
public struct ColorToken: Sendable, Equatable {
    /// The design's name for it, `surface.window`.
    public let name: String
    /// 0xRRGGBB in the light appearance.
    public let light: UInt32
    /// 0xRRGGBB in the dark appearance.
    public let dark: UInt32

    public init(_ name: String, light: UInt32, dark: UInt32) {
        self.name = name
        self.light = light
        self.dark = dark
    }

    /// The token as a SwiftUI color that follows the window's appearance.
    public var color: Color {
        let light = light
        let dark = dark
        return Color(
            nsColor: NSColor(name: NSColor.Name(name)) { appearance in
                let isDark = appearance.bestMatch(from: [.aqua, .darkAqua]) == .darkAqua
                return NSColor(rgb: isDark ? dark : light)
            })
    }
}

extension NSColor {
    convenience init(rgb: UInt32) {
        self.init(
            srgbRed: CGFloat((rgb >> 16) & 0xFF) / 255,
            green: CGFloat((rgb >> 8) & 0xFF) / 255,
            blue: CGFloat(rgb & 0xFF) / 255,
            alpha: 1)
    }
}

/// The app's colors, by what they are for.
public enum Palette {
    public static let windowToken = ColorToken("surface.window", light: 0xF7F7F5, dark: 0x131415)
    public static let cardToken = ColorToken("surface.card", light: 0xFFFFFF, dark: 0x18191B)
    public static let controlToken = ColorToken("surface.control", light: 0xECECE8, dark: 0x222326)
    public static let codeToken = ColorToken("surface.code", light: 0xF3F3F0, dark: 0x0F1011)
    public static let borderToken = ColorToken("line.border", light: 0xDDDDD7, dark: 0x2A2C2F)
    public static let textPrimaryToken = ColorToken("text.primary", light: 0x1B1C1D, dark: 0xE7E5E0)
    public static let textSecondaryToken = ColorToken("text.secondary", light: 0x4B4C4E, dark: 0xABA8A0)
    public static let textTertiaryToken = ColorToken("text.tertiary", light: 0x5F6062, dark: 0x938F86)
    public static let accentToken = ColorToken("status.accent", light: 0x0B7A68, dark: 0x7FD4C1)
    public static let warningToken = ColorToken("status.warning", light: 0x9F5600, dark: 0xE9A23B)
    public static let errorToken = ColorToken("status.error", light: 0xB3261E, dark: 0xF2836F)

    /// Every token above, for the tests that pin them.
    public static let tokens: [ColorToken] = [
        windowToken, cardToken, controlToken, codeToken, borderToken,
        textPrimaryToken, textSecondaryToken, textTertiaryToken,
        accentToken, warningToken, errorToken,
    ]

    public static var window: Color { windowToken.color }
    public static var card: Color { cardToken.color }
    public static var control: Color { controlToken.color }
    public static var code: Color { codeToken.color }
    public static var border: Color { borderToken.color }
    public static var textPrimary: Color { textPrimaryToken.color }
    public static var textSecondary: Color { textSecondaryToken.color }
    public static var textTertiary: Color { textTertiaryToken.color }
    public static var accent: Color { accentToken.color }
    public static var warning: Color { warningToken.color }
    public static var error: Color { errorToken.color }
}
