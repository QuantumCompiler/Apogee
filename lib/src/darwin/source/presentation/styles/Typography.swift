import SwiftUI

/// The design's type scale, in points (tokens.json -> type.scalePx), set in
/// the system fonts -- SF Pro and SF Mono -- in native macOS restraint, and
/// scaled by the person's text size (Appearance).
public enum TypeScale {
    public static let display: CGFloat = 24
    public static let h1: CGFloat = 19
    public static let h2: CGFloat = 16
    public static let h3: CGFloat = 14
    public static let title: CGFloat = 13.5
    public static let prose: CGFloat = 14
    public static let body: CGFloat = 13
    public static let small: CGFloat = 12
    public static let code: CGFloat = 12
    public static let meta: CGFloat = 11
    public static let label: CGFloat = 10.5

    /// Every step, largest first, for the tests that pin the scale.
    public static let steps: [CGFloat] = [display, h1, h2, h3, prose, title, body, small, meta, label]

    /// A step at the person's text size, rounded to the half point (tokens.json -> type.rounding).
    public static func scaled(_ size: CGFloat) -> CGFloat {
        (size * Appearance.scale * 2).rounded() / 2
    }
}

extension Font {
    public static var apogeeDisplay: Font { .system(size: TypeScale.scaled(TypeScale.display), weight: .semibold) }
    public static var apogeeHeading: Font { .system(size: TypeScale.scaled(TypeScale.h2), weight: .semibold) }
    public static var apogeeTitle: Font { .system(size: TypeScale.scaled(TypeScale.title), weight: .medium) }
    public static var apogeeProse: Font { .system(size: TypeScale.scaled(TypeScale.prose)) }
    public static var apogeeBody: Font { .system(size: TypeScale.scaled(TypeScale.body)) }
    public static var apogeeSmall: Font { .system(size: TypeScale.scaled(TypeScale.small)) }
    public static var apogeeMeta: Font { .system(size: TypeScale.scaled(TypeScale.meta)) }
    public static var apogeeCode: Font { .system(size: TypeScale.scaled(TypeScale.code), design: .monospaced) }
}
