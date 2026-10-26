#if DEBUG
/// The binary's own documents for this module's previews, copied byte for byte
/// from the recorded test fixtures named beside each (ADR 0004). A re-recorded
/// fixture is copied here again in the same change.
enum SuitesDocuments {
    /// tests/business/reads/Fixtures/suites-config-get.txt
    static let configGetSuites = #"""
cloud
fast
pair
shared
vast
"""#
    /// tests/business/reads/Fixtures/suites-config-get-cloud.txt
    static let configGetCloud = #"""
chat: claude
embedding: helper
"""#
    /// tests/business/reads/Fixtures/suites-config-get-fast.txt
    static let configGetFast = #"""
chat: fastroot
utility: fasthelper
"""#
    /// tests/business/reads/Fixtures/suites-config-get-pair.txt
    static let configGetPair = #"""
chat: plain
utility: fasthelper
"""#
    /// tests/business/reads/Fixtures/suites-config-get-shared.txt
    static let configGetShared = #"""
chat: root
extraction: scribe
utility: helper
"""#
    /// tests/business/reads/Fixtures/suites-config-get-vast.txt
    static let configGetVast = #"""
chat: root
extraction: vast
"""#
    /// tests/business/reads/Fixtures/suites-models-status-cloud.json
    static let statusCloud = #"""
{"fields":[{"field":"suite","value":"cloud"},{"field":"chat","value":"claude   (via suite cloud)"},{"field":"embedding","value":"helper   (via suite cloud)   [local: 40960 MiB of weights, 136 MiB of cache]"},{"field":"extraction","value":"plain   (via models.default)"},{"field":"vision","value":"(unset -- the chat's own backend)"},{"field":"transcription","value":"(unset -- the chat's own backend)"},{"field":"utility","value":"(unset -- the chat's own backend)"},{"field":"footprint","value":"suite cloud\nclaude (chat): nothing held here\nhelper (embedding): 41096 MiB -- 40960 MiB of weights, 136 MiB of q8_0 cache at 8192 tokens, 0 MiB of adapters\ntotal: suite cloud needs 42120 MiB of this machine's 110100 MiB: helper 41096 + 1024 margin -- fits"}]}
"""#
    /// tests/business/reads/Fixtures/suites-models-status-fast.json
    static let statusFast = #"""
{"fields":[{"field":"suite","value":"fast"},{"field":"chat","value":"fastroot   (via suite fast)"},{"field":"embedding","value":"plain   (via models.default)"},{"field":"extraction","value":"plain   (via models.default)"},{"field":"vision","value":"(unset -- the chat's own backend)"},{"field":"transcription","value":"(unset -- the chat's own backend)"},{"field":"utility","value":"fasthelper   (via suite fast)"},{"field":"footprint","value":"suite fast\nfastroot (chat): nothing held here\nfasthelper (utility): nothing held here\ntotal: suite fast: no member holds memory on this machine"}]}
"""#
    /// tests/business/reads/Fixtures/suites-models-status-pair.json
    static let statusPair = #"""
{"fields":[{"field":"suite","value":"pair"},{"field":"chat","value":"plain   (via suite pair)"},{"field":"embedding","value":"plain   (via models.default)"},{"field":"extraction","value":"plain   (via models.default)"},{"field":"vision","value":"(unset -- the chat's own backend)"},{"field":"transcription","value":"(unset -- the chat's own backend)"},{"field":"utility","value":"fasthelper   (via suite pair)"},{"field":"footprint","value":"suite pair\nplain (chat): nothing held here\nfasthelper (utility): nothing held here\ntotal: suite pair: no member holds memory on this machine"}]}
"""#
    /// tests/business/reads/Fixtures/suites-models-status-shared.json
    static let statusShared = #"""
{"fields":[{"field":"suite","value":"shared"},{"field":"chat","value":"root   (via suite shared)   [local: 40960 MiB of weights, 136 MiB of cache]"},{"field":"embedding","value":"plain   (via models.default)"},{"field":"extraction","value":"scribe   (via suite shared)   [local: 40960 MiB of weights, 136 MiB of cache]"},{"field":"vision","value":"(unset -- the chat's own backend)"},{"field":"transcription","value":"(unset -- the chat's own backend)"},{"field":"utility","value":"helper   (via suite shared)   [local: 40960 MiB of weights, 136 MiB of cache]"},{"field":"footprint","value":"suite shared\nroot (chat): 41096 MiB -- 40960 MiB of weights, 136 MiB of q8_0 cache at 8192 tokens\nscribe (extraction): 136 MiB -- shares root's 40960 MiB of weights, 136 MiB of q8_0 cache at 8192 tokens\nhelper (utility): 136 MiB -- shares root's 40960 MiB of weights, 136 MiB of q8_0 cache at 8192 tokens, 0 MiB of adapters\ntotal: suite shared needs 42392 MiB of this machine's 110100 MiB: root 41096 + scribe 136 (shares root's weights, 40960 MiB saved) + helper 136 (shares root's weights, 40960 MiB saved) + 1024 margin -- fits"}]}
"""#
    /// tests/business/reads/Fixtures/suites-models-status-vast.json
    static let statusVast = #"""
{"fields":[{"field":"suite","value":"vast"},{"field":"chat","value":"root   (via suite vast)   [local: 40960 MiB of weights, 136 MiB of cache]"},{"field":"embedding","value":"plain   (via models.default)"},{"field":"extraction","value":"vast   (via suite vast)   [local: 204800 MiB of weights, 136 MiB of cache]"},{"field":"vision","value":"(unset -- the chat's own backend)"},{"field":"transcription","value":"(unset -- the chat's own backend)"},{"field":"utility","value":"(unset -- the chat's own backend)"},{"field":"footprint","value":"suite vast\nroot (chat): 41096 MiB -- 40960 MiB of weights, 136 MiB of q8_0 cache at 8192 tokens\nvast (extraction): 204936 MiB -- 204800 MiB of weights, 136 MiB of q8_0 cache at 8192 tokens\ntotal: suite vast needs 247056 MiB of this machine's 110100 MiB: root 41096 + vast 204936 + 1024 margin -- it does not fit"}]}
"""#
    /// tests/business/reads/Fixtures/suites-models-status.json
    static let status = #"""
{"fields":[{"field":"suite","value":"fast"},{"field":"chat","value":"fastroot   (via suite fast)"},{"field":"embedding","value":"plain   (via models.default)"},{"field":"extraction","value":"plain   (via models.default)"},{"field":"vision","value":"(unset -- the chat's own backend)"},{"field":"transcription","value":"(unset -- the chat's own backend)"},{"field":"utility","value":"fasthelper   (via suite fast)"},{"field":"footprint","value":"suite fast\nfastroot (chat): nothing held here\nfasthelper (utility): nothing held here\ntotal: suite fast: no member holds memory on this machine"}]}
"""#
    /// tests/business/reads/Fixtures/suites-models-list.json
    static let modelsList = #"""
{"data":[{"architecture":"-","backend":"claude","backend_type":"anthropic","group":"anthropic","model":"claude-sonnet-4-5","note":"no API key found; run 'apogee auth add anthropic'","profile":"unprofiled","roles":"","section":"cloud","source":"-","state":"no key","type":"model","verified":"no turn yet"},{"architecture":"-","backend":"fasthelper","backend_type":"mock","group":"other","model":"","profile":"unprofiled","roles":"","section":"local","source":"-","state":"-","type":"model","verified":"-"},{"architecture":"-","backend":"fastroot","backend_type":"mock","group":"other","model":"","profile":"unprofiled","roles":"chat","section":"local","source":"-","state":"-","type":"model","verified":"-"},{"architecture":"llama","backend":"helper","backend_type":"llamacpp","format":"gguf","group":"gguf","model":"base-Q4_K_M.gguf","profile":"llama3 (unverified)","roles":"","section":"local","source":"local","state":"ok","type":"model","verified":"no digest published"},{"architecture":"-","backend":"plain","backend_type":"mock","group":"other","model":"","profile":"unprofiled","roles":"embedding, extraction","section":"local","source":"-","state":"-","type":"model","verified":"-"},{"architecture":"llama","backend":"root","backend_type":"llamacpp","format":"gguf","group":"gguf","model":"base-Q4_K_M.gguf","profile":"llama3 (unverified)","roles":"","section":"local","source":"local","state":"ok","type":"model","verified":"no digest published"},{"architecture":"llama","backend":"scribe","backend_type":"llamacpp","format":"gguf","group":"gguf","model":"base-Q4_K_M.gguf","profile":"llama3 (unverified)","roles":"","section":"local","source":"local","state":"ok","type":"model","verified":"no digest published"},{"architecture":"llama","backend":"vast","backend_type":"llamacpp","format":"gguf","group":"gguf","model":"vast-F16.gguf","profile":"llama3 (unverified)","roles":"","section":"local","source":"local","state":"ok","type":"model","verified":"no digest published"}],"folded":0,"object":"list"}
"""#
    /// tests/business/reads/Fixtures/suites-system.json
    static let system = #"""
{"apogee":{"models_held":[],"process_bytes":3949072},"cpu":{"architecture":"arm64","efficiency_cores":4,"load":{"fifteen_minutes":2.72,"five_minutes":3.22,"one_minute":4.18},"load_unknown":null,"logical_cores":16,"model":"Apple M3 Max","performance_cores":12,"physical_cores":16,"utilization":{"percent":2.36,"window_ms":500}},"disk":{"store":"/Users/someone/.apogee/models","store_bytes":257698037868,"volume_available_bytes":4982360883200,"volume_capacity_bytes":7998551654400},"gpu":{"model_budget_bytes":115448725504,"model_budget_unknown":null,"name":"Apple M3 Max","unified_memory":true,"unknown":null},"memory":{"available_bytes":109522403328,"total_bytes":137438953472,"used_bytes":27916550144}}
"""#
    /// tests/business/reads/Fixtures/suites-models-info-root.json
    static let infoRoot = #"""
{"fields":[{"field":"backend","value":"root"},{"field":"type","value":"llamacpp"},{"field":"roles","value":"-"},{"field":"model_path","value":"/Users/someone/.apogee/models/base/gguf/dddddddddddd/base-Q4_K_M.gguf"},{"field":"lineage","value":"unknown -- no record says where it came from"},{"field":"header","value":"ok (GGUF v3)"},{"field":"architecture","value":"llama"},{"field":"profile","value":"llama3 (unverified)"},{"field":"evidence","value":"not verified: the llama3.2 files available carry no chat template and degenerate on every prompt -- likely base models. Registered so the family is a declared gap, not a silent one"},{"field":"tensors","value":"1 total, 1 text"},{"field":"size","value":"40960 MiB"},{"field":"template","value":"none, so it is most likely a base (pretrained) model: it continues text rather than answering, so what it says can be confidently wrong, and it cannot use tools. For chat, use its instruction-tuned release, usually named '-it' or '-Instruct'"},{"field":"window","value":"8192 tokens (the default; trained for 8192)"},{"field":"cache","value":"136 MiB at q8_0 (the default)"},{"field":"sampling","value":"temperature 0.6 (family) · top-p 0.9 (family) · top-k 0 (default) · min-p 0 (default) · repeat penalty 1 (default) · presence penalty 0 (default) · seed drawn per answer -- family values from the Llama 3.1 and 3.2 Instruct generation config: temperature 0.6, top-p 0.9"},{"field":"thinking","value":"on (default), no budget -- off renders the template's own switch, where it has one"}],"name":"root"}
"""#
    /// tests/business/reads/Fixtures/suites-models-info-helper.json
    static let infoHelper = #"""
{"fields":[{"field":"backend","value":"helper"},{"field":"type","value":"llamacpp"},{"field":"roles","value":"-"},{"field":"model_path","value":"/Users/someone/.apogee/models/base/gguf/dddddddddddd/base-Q4_K_M.gguf"},{"field":"lineage","value":"unknown -- no record says where it came from"},{"field":"header","value":"ok (GGUF v3)"},{"field":"architecture","value":"llama"},{"field":"profile","value":"llama3 (unverified)"},{"field":"evidence","value":"not verified: the llama3.2 files available carry no chat template and degenerate on every prompt -- likely base models. Registered so the family is a declared gap, not a silent one"},{"field":"tensors","value":"1 total, 1 text"},{"field":"size","value":"40960 MiB"},{"field":"template","value":"none, so it is most likely a base (pretrained) model: it continues text rather than answering, so what it says can be confidently wrong, and it cannot use tools. For chat, use its instruction-tuned release, usually named '-it' or '-Instruct'"},{"field":"window","value":"8192 tokens (the default; trained for 8192)"},{"field":"cache","value":"136 MiB at q8_0 (the default)"},{"field":"sampling","value":"temperature 0.6 (family) · top-p 0.9 (family) · top-k 0 (default) · min-p 0 (default) · repeat penalty 1 (default) · presence penalty 0 (default) · seed drawn per answer -- family values from the Llama 3.1 and 3.2 Instruct generation config: temperature 0.6, top-p 0.9"},{"field":"thinking","value":"on (default), no budget -- off renders the template's own switch, where it has one"}],"name":"helper"}
"""#
    /// tests/business/reads/Fixtures/suites-models-info-scribe.json
    static let infoScribe = #"""
{"fields":[{"field":"backend","value":"scribe"},{"field":"type","value":"llamacpp"},{"field":"roles","value":"-"},{"field":"model_path","value":"/Users/someone/.apogee/models/base/gguf/dddddddddddd/base-Q4_K_M.gguf"},{"field":"lineage","value":"unknown -- no record says where it came from"},{"field":"header","value":"ok (GGUF v3)"},{"field":"architecture","value":"llama"},{"field":"profile","value":"llama3 (unverified)"},{"field":"evidence","value":"not verified: the llama3.2 files available carry no chat template and degenerate on every prompt -- likely base models. Registered so the family is a declared gap, not a silent one"},{"field":"tensors","value":"1 total, 1 text"},{"field":"size","value":"40960 MiB"},{"field":"template","value":"none, so it is most likely a base (pretrained) model: it continues text rather than answering, so what it says can be confidently wrong, and it cannot use tools. For chat, use its instruction-tuned release, usually named '-it' or '-Instruct'"},{"field":"window","value":"8192 tokens (the default; trained for 8192)"},{"field":"cache","value":"136 MiB at q8_0 (the default)"},{"field":"sampling","value":"temperature 0.6 (family) · top-p 0.9 (family) · top-k 0 (default) · min-p 0 (default) · repeat penalty 1 (default) · presence penalty 0 (default) · seed drawn per answer -- family values from the Llama 3.1 and 3.2 Instruct generation config: temperature 0.6, top-p 0.9"},{"field":"thinking","value":"on (default), no budget -- off renders the template's own switch, where it has one"}],"name":"scribe"}
"""#
    /// tests/business/reads/Fixtures/suites-models-info-vast.json
    static let infoVast = #"""
{"fields":[{"field":"backend","value":"vast"},{"field":"type","value":"llamacpp"},{"field":"roles","value":"-"},{"field":"model_path","value":"/Users/someone/.apogee/models/vast/gguf/eeeeeeeeeeee/vast-F16.gguf"},{"field":"lineage","value":"unknown -- no record says where it came from"},{"field":"header","value":"ok (GGUF v3)"},{"field":"architecture","value":"llama"},{"field":"profile","value":"llama3 (unverified)"},{"field":"evidence","value":"not verified: the llama3.2 files available carry no chat template and degenerate on every prompt -- likely base models. Registered so the family is a declared gap, not a silent one"},{"field":"tensors","value":"1 total, 1 text"},{"field":"size","value":"204800 MiB"},{"field":"template","value":"none, so it is most likely a base (pretrained) model: it continues text rather than answering, so what it says can be confidently wrong, and it cannot use tools. For chat, use its instruction-tuned release, usually named '-it' or '-Instruct'"},{"field":"window","value":"8192 tokens (the default; trained for 8192)"},{"field":"cache","value":"136 MiB at q8_0 (the default)"},{"field":"sampling","value":"temperature 0.6 (family) · top-p 0.9 (family) · top-k 0 (default) · min-p 0 (default) · repeat penalty 1 (default) · presence penalty 0 (default) · seed drawn per answer -- family values from the Llama 3.1 and 3.2 Instruct generation config: temperature 0.6, top-p 0.9"},{"field":"thinking","value":"on (default), no budget -- off renders the template's own switch, where it has one"}],"name":"vast"}
"""#
}
#endif
