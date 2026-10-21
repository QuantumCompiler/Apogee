#if DEBUG
import BusinessReads
import Foundation

/// The Suites place's view-model for its previews (ADR 0004), over the
/// documents tests/presentation/suites/SuitesViewModelTests.swift holds: no
/// suite is recorded from the binary, so its tests' own `config get`, `models
/// status`, `models list`, `system` and `models info` are the documents.
/// Nothing starts the binary.
@MainActor
enum SuitesPreview {
    /// What `config get suites.<name>` prints, by name: `coding`, a GGUF chat
    /// model and a small helper, too big together for the 8 GiB budget, and
    /// the default; `research`, a cloud API and an MLX embedder whose record
    /// states no size; `duo`, one backend in two roles, held once, within the
    /// budget (the tests' `heldOnce`).
    static let texts = [
        "coding": "chat: qwen\nutility: small (window 4096)\n",
        "research": "chat: claude\nembedding: mlx-embed\n",
        "duo": "chat: small\nutility: small\n",
    ]

    /// `coding` and `research`, as the tests' `scripted()` answers.
    static let both = runner(["coding", "research"])
    /// `duo` alone.
    static let one = runner(["duo"])
    /// No suite configured: the tests' `empty`.
    static let empty = runner([])

    /// A suite, as the view-model reads it from its `config get` text.
    static func suite(_ name: String) -> Suite {
        Suite(name: name, text: texts[name] ?? "")
    }

    static func model(over runner: PreviewRunner = both) -> SuitesViewModel {
        SuitesViewModel(runner: runner)
    }

    /// `config get suites` naming `names`, each one's `config get
    /// suites.<name>`, and the reads every suite's fit is worked out from.
    static func runner(_ names: [String]) -> PreviewRunner {
        PreviewRunner(
            reads: [
                (["models", "status"], #"{"fields":[{"field":"suite","value":"coding"},{"field":"chat","value":"qwen   (via suite coding)"}]}"#),
                (["models", "list"], #"{"object":"list","data":[{"backend":"qwen","backend_type":"llamacpp"},{"backend":"small","backend_type":"llamacpp"},{"backend":"claude","backend_type":"anthropic"},{"backend":"mlx-embed","backend_type":"mlx"}]}"#),
                (["system"], #"{"gpu":{"model_budget_bytes":8589934592}}"#),
                (["models", "info", "qwen"], #"{"name":"qwen","fields":[{"field":"size","value":"4800 MiB"},{"field":"cache","value":"4608 MiB at f16 (the default)"}]}"#),
                (["models", "info", "small"], #"{"name":"small","fields":[{"field":"size","value":"600 MiB"},{"field":"cache","value":"none -- no attention layers"}]}"#),
                (["models", "info", "mlx-embed"], #"{"name":"mlx-embed","fields":[{"field":"format","value":"MLX"}]}"#),
            ],
            commands: [(["config", "get", "suites"], names.map { $0 + "\n" }.joined())]
                + names.map { (["config", "get", "suites.\($0)"], texts[$0] ?? "") })
    }
}
#endif
