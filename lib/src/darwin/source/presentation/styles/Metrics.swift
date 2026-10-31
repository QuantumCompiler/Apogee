import CoreGraphics

/// The spacing steps, in points (tokens.json -> spacingPx).
public enum Spacing {
    public static let xxs: CGFloat = 2
    public static let xs: CGFloat = 4
    public static let s: CGFloat = 8
    public static let m: CGFloat = 12
    public static let l: CGFloat = 16
    public static let xl: CGFloat = 24
    public static let xxl: CGFloat = 32
}

/// Corner radii, in points (tokens.json -> radiusPx).
public enum Radius {
    public static let tag: CGFloat = 3
    public static let chip: CGFloat = 4
    public static let row: CGFloat = 5
    public static let control: CGFloat = 6
    public static let card: CGFloat = 8
    public static let dialog: CGFloat = 12
}

/// Window and column sizes, in points (tokens.json -> layoutPx).
public enum Layout {
    public static let windowMinWidth: CGFloat = 960
    public static let windowMinHeight: CGFloat = 640
    /// The widest a conversation's column grows.
    public static let conversationMax: CGFloat = 860
    /// The sidebar, and the column of icons it becomes in a narrow window.
    public static let sidebar: CGFloat = 256
    public static let sidebarCollapsed: CGFloat = 56
    /// A screen's header: its title and its own controls.
    public static let header: CGFloat = 52
    /// A place's button in the sidebar.
    public static let placeWidth: CGFloat = 40
    public static let placeHeight: CGFloat = 34
    /// A place's button padding in the sidebar.
    public static let placePadding: CGFloat = 12
    /// The widest the media screens' content grows.
    public static let mediaContentMax: CGFloat = 820
}
