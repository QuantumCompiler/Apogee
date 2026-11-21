/**
 * The native-controls table (design 1r): what each piece of the design becomes
 * in SwiftUI, Qt Quick and WinUI 3, the old BuildLayout board's at 1c's frozen
 * revision `b375fac9`, converted once and kept here as data. A new piece is one
 * entry. Its words are the board's but for the SwiftUI header's macOS, which
 * names the Mac app's real floor, its Xcode project's `MACOSX_DEPLOYMENT_TARGET`
 * (a test holds the two together): the board said 14.
 */

/** One piece of the design and what it becomes on each platform. */
export interface NativeControl {
  readonly design: string
  readonly swiftui: string
  readonly qtQuick: string
  readonly winui: string
}

/** The table's column headers. */
export const nativeControlsHead: NativeControl = { design: 'THE DESIGN', swiftui: 'SWIFTUI · MACOS 27', qtQuick: 'QT QUICK · LINUX', winui: 'WINUI 3 · WINDOWS' }

/** The board's 16 rows, in its order. */
export const nativeControls: readonly NativeControl[] = [
  { design: 'Sidebar and content', swiftui: 'NavigationSplitView', qtQuick: 'SplitView and ListView', winui: 'NavigationView, left' },
  { design: 'Date groups, inspector sections', swiftui: 'DisclosureGroup', qtQuick: 'a header delegate that toggles its section', winui: 'Expander' },
  { design: 'Places bar', swiftui: 'a horizontal ScrollView of Buttons, each with .help', qtQuick: 'ListView, orientation horizontal', winui: 'ItemsRepeater in a horizontal ScrollViewer' },
  { design: 'Inspector', swiftui: '.inspector(isPresented:)', qtQuick: 'a SplitView pane', winui: 'a SplitView pane' },
  { design: 'Segmented choices', swiftui: 'Picker, .segmented', qtQuick: 'a ButtonGroup of Buttons', winui: 'SelectorBar' },
  { design: 'Model and thinking menus', swiftui: 'Menu with Sections', qtQuick: 'Menu', winui: 'MenuFlyout on a DropDownButton' },
  { design: 'Sheets: download, new task, install, confirm', swiftui: '.sheet, .confirmationDialog', qtQuick: 'a modal Dialog', winui: 'ContentDialog' },
  { design: 'Settings', swiftui: 'a Settings scene with its own sidebar', qtQuick: 'a separate Window', winui: 'a settings page' },
  { design: 'Toast', swiftui: 'an overlay with a timer', qtQuick: 'a Popup', winui: 'InfoBar' },
  { design: 'Composer', swiftui: 'TextEditor with onKeyPress', qtQuick: 'TextArea', winui: 'TextBox, AcceptsReturn' },
  { design: 'Conversation', swiftui: 'ScrollView and LazyVStack, .defaultScrollAnchor(.bottom)', qtQuick: 'ListView, BottomToTop', winui: 'ItemsRepeater, kept at the end' },
  { design: 'An answer’s Markdown', swiftui: 'AttributedString(markdown:) and a code view', qtQuick: 'Text, MarkdownText', winui: 'MarkdownTextBlock' },
  { design: 'Video player', swiftui: 'AVPlayerView under the design’s controls', qtQuick: 'MediaPlayer and VideoOutput', winui: 'MediaPlayerElement' },
  { design: 'Waveform, timeline, code graph', swiftui: 'Canvas', qtQuick: 'Canvas or QQuickPaintedItem', winui: 'Win2D CanvasControl' },
  { design: 'Colour wells', swiftui: 'ColorPicker', qtQuick: 'ColorDialog', winui: 'ColorPicker' },
  { design: 'The children', swiftui: 'Process with Pipes, one reader per stream', qtQuick: 'QProcess', winui: 'CreateProcess with anonymous pipes' },
]
