import Foundation

/// A backend's recorded consent for its vendor to run tools of its own under
/// the person's key -- the CLI's 47a `server_tools`, `web_search` and
/// `code_execution` -- as `config get backends.<name>.server_tools` prints it
/// since the CLI's 55g: one name a line, nothing when none was consented.
/// Read, never written here: the consent is the person's, given through
/// `config set-backend --server-tools`.
public enum ServerTools {
    /// The backend types whose vendor runs tools of its own: the three APIs.
    /// The CLI refuses `server_tools` on every other type, so no other entry
    /// is asked (47a).
    public static let vendorTypes: Set<String> = ["anthropic", "openai", "google"]

    /// Whether a backend of `backendType` can hold the consent.
    public static func held(by backendType: String) -> Bool {
        vendorTypes.contains(backendType)
    }

    /// The key `config get` reads it by.
    public static func key(of backend: String) -> String {
        "backends.\(backend).server_tools"
    }

    /// The tools `config get` printed, in order; none for an empty answer.
    public static func list(_ text: String) -> [String] {
        text.split(whereSeparator: \.isNewline)
            .map { $0.trimmingCharacters(in: .whitespaces) }
            .filter { !$0.isEmpty }
    }
}
