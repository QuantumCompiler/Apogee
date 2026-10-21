#if DEBUG
/// The binary's own documents for this module's previews, copied byte for byte
/// from the recorded test fixtures named beside each (ADR 0004). A re-recorded
/// fixture is copied here again in the same change.
enum ConsoleDocuments {
    /// tests/business/reads/Fixtures/config-scan.txt
    static let configScan = #"""
Llama3.2-1B-Instruct-F16       not registered
                               /Users/someone/.apogee/models/Llama3.2-1B-Instruct/gguf/3f0c2a9b7e41/Llama3.2-1B-Instruct-F16.gguf
Llama3.2-1B-Instruct-Q4KM      backend: llama3.2-1b-Q4KM
                               /Users/someone/.apogee/models/Llama3.2-1B-Instruct/gguf/8d51e6c04a27/Llama3.2-1B-Instruct-Q4KM.gguf
Gemma4-26B-Instruct-mixed_4_8  not registered
                               /Users/someone/.apogee/models/Gemma4-26B-Instruct/mlx/a88b9f386aff
2 SafeTensors snapshot(s) not listed -- weights, not runnable; each registers when converted

register the unregistered ones as backends: apogee config scan --register
"""#
    /// tests/business/reads/Fixtures/symphony-play.err
    static let symphonyPlayErr = #"""
stage 1/2 critique — asking utility (writer): Below is a draft. Work out what it is meant to do -- from… · 0.0 s
stage 2/2 revise — asking chat (writer): Here is a draft and a critique of it. Draft: A draft README… · 0.0 s
A tighter draft, for new contributors: what the tool does, then how to run it.
"""#
    /// tests/business/reads/Fixtures/review-no-gh.err
    static let reviewNoGh = #"""
[review] reading github.com/acme/widget#12
apogee review: gh is not installed, or not on PATH (the GitHub CLI is how Apogee reaches github, under its own login) -- install the GitHub CLI (https://cli.github.com), then: gh auth login
"""#
}
#endif
