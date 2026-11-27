import Foundation

/// The CLI's install channel a build of the app drives (GUI M18): one per
/// build, decided when it is built, as the CLI bakes its own.
///
/// Development runs on the dev channel (the user's call, 2026-10-31): the
/// release `apogee`, `~/.apogee` and the installed app are the production
/// install, changed only by what a release ships. So a development build
/// drives `apogee-dev` alone and never falls back to the release binary, while
/// the release build looks where it always has. Discovery takes the channel as
/// a value; only the composition root reads it, from the bundle's
/// `ApogeeChannel` (ADR 0001: nothing below it reads a build setting).
public enum InstallChannel: String, Sendable, Equatable, CaseIterable {
    /// The release build -- what `build.sh build` and `package`, CI and a
    /// release carry: `~/.local/bin/apogee` first, then the dev and test
    /// channels beside it.
    case release
    /// A development build -- Xcode's Run and `build.sh open`, "Apogee Dev":
    /// `~/.local/bin/apogee-dev` alone. No `apogee-test` fallback; a
    /// test-channel app is out of scope until asked (M18's recorded default).
    case dev

    /// The channel a bundle's `ApogeeChannel` value names: `dev` is the dev
    /// channel; anything else, or none -- a bundle built before the key
    /// existed -- the release channel, as the mod reads its own option.
    public init(bundleValue: Any?) {
        self = (bundleValue as? String).flatMap(InstallChannel.init(rawValue:)) ?? .release
    }

    /// How to install this channel's binary, said where none was found: the
    /// README's one-liner for the release; for the dev channel, the CLI's own
    /// dev install from a checkout.
    public var installHint: String {
        switch self {
        case .release:
            "Install Apogee with: curl -fsSL https://raw.githubusercontent.com/QuantumCompiler/Apogee/stable/lib/scripts/install.sh | bash"
        case .dev:
            "Install the dev channel with: make -C lib/src/cli install MODE=dev"
        }
    }
}
