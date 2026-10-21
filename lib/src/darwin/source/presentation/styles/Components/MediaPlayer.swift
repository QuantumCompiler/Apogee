import AVKit
import SwiftUI

/// A clip or a composition, played with the system's own controls.
///
/// Built on `AVPlayerView` itself rather than SwiftUI's `VideoPlayer`: that
/// links only its SwiftUI shim, so AVKit -- whose view the shim subclasses --
/// was never loaded, and the first clip page would have crashed the app
/// (found drawing it offscreen, 2026-10-20). Naming the view here links AVKit
/// into `styles`, and every module that plays media uses this.
public struct MediaPlayer: NSViewRepresentable {
    private let url: URL

    public init(url: URL) {
        self.url = url
    }

    public func makeNSView(context: Context) -> AVPlayerView {
        let view = AVPlayerView()
        view.controlsStyle = .inline
        view.player = AVPlayer(url: url)
        return view
    }

    public func updateNSView(_ view: AVPlayerView, context: Context) {
        if (view.player?.currentItem?.asset as? AVURLAsset)?.url != url {
            view.player = AVPlayer(url: url)
        }
    }

    public static func dismantleNSView(_ view: AVPlayerView, coordinator: ()) {
        view.player?.pause()
    }
}

#if DEBUG
#Preview("Nothing loaded") {
    // No clip of the person's: an empty player, its controls drawn.
    MediaPlayer(url: URL(fileURLWithPath: "/dev/null"))
        .frame(width: 640, height: 360)
}
#endif
