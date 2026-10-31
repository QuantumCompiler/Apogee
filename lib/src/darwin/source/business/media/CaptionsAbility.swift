import DataProtocol
import Foundation

/// Whether this machine's ffmpeg burns a composition's captions into its
/// frames, as `check`'s `ffmpeg captions` row says it (the CLI's 52e): `ok`
/// when ffmpeg has the `subtitles` filter (libass), `skipped` -- with the
/// command that brings it -- when it has not, and the composer keeps the
/// captions beside the video as an SRT instead. What the sheet says before
/// a composition runs (GUI 4f); the CLI decides, the app only says it.
public struct CaptionsAbility: Sendable, Equatable {
    /// The row's name in `check`'s `Video` section.
    public static let row = "ffmpeg captions"

    /// Whether the captions burn in.
    public let burns: Bool
    /// The row's detail, in the CLI's words.
    public let detail: String
    /// What brings the ability, where the row names it.
    public let remedy: String?

    public init(burns: Bool, detail: String, remedy: String?) {
        self.burns = burns
        self.detail = detail
        self.remedy = remedy
    }

    /// The ability `check --output-format json`'s document states; `nil`
    /// where it has no such row -- a binary before the CLI's 52e.
    public init?(check document: JSONValue) {
        guard let row = (document["rows"]?.arrayValue ?? []).first(where: {
            $0["section"]?.stringValue == "Video" && $0["name"]?.stringValue == Self.row
        }) else { return nil }
        burns = row["status"]?.stringValue == "ok"
        detail = row["detail"]?.stringValue ?? ""
        remedy = row["remedy"]?.stringValue
    }

    /// Why, in the CLI's words before its own consequence: `no subtitles
    /// filter (this ffmpeg was built without libass)`.
    public var reason: String {
        detail.components(separatedBy: " -- ").first ?? detail
    }
}
