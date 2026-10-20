import SwiftUI

/// The design's type scale, in points (tokens.json -> type.scalePx), set in
/// the system fonts -- SF Pro and SF Mono -- in native macOS restraint.
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
}

extension Font {
    public static let apogeeDisplay = Font.system(size: TypeScale.display, weight: .semibold)
    public static let apogeeHeading = Font.system(size: TypeScale.h2, weight: .semibold)
    public static let apogeeTitle = Font.system(size: TypeScale.title, weight: .medium)
    public static let apogeeProse = Font.system(size: TypeScale.prose)
    public static let apogeeBody = Font.system(size: TypeScale.body)
    public static let apogeeSmall = Font.system(size: TypeScale.small)
    public static let apogeeMeta = Font.system(size: TypeScale.meta)
    public static let apogeeCode = Font.system(size: TypeScale.code, design: .monospaced)
}
