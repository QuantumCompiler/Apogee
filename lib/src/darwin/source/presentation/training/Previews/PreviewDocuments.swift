#if DEBUG
/// The binary's own documents for this module's previews, copied byte for byte
/// from the recorded test fixtures named beside each (ADR 0004). A re-recorded
/// fixture is copied here again in the same change.
enum TrainingDocuments {
    /// tests/business/reads/Fixtures/train-status.json
    static let trainStatus = #"""
{
 "active_pipeline": null,
 "cycle": null,
 "cycle_active": false,
 "latest_pipeline": null,
 "pipelines": 0,
 "recent_runs": [
  {
   "base_model": "/Users/someone/work/student",
   "dataset": "/Users/someone/work/data.jsonl",
   "eval_passed": false,
   "eval_score": 0.4583333333333333,
   "final_loss": 0.42,
   "finished_at": "2026-10-21T05:18:26Z",
   "id": "20261021-051826",
   "iterations": 4,
   "kind": "run",
   "method": "lora",
   "started_at": "2026-10-21T05:18:26Z",
   "status": "complete",
   "trainer": "mock"
  }
 ],
 "running": [],
 "runs": 1,
 "versions": []
}
"""#
    /// tests/business/reads/Fixtures/train-regimes.json
    static let trainRegimes = #"""
{
 "data": [
  {
   "config": false,
   "count": 0,
   "description": "The four general kits as one gated pipeline -- instruction-following, structured output, summarization, then reasoning.",
   "iters": 0,
   "kits": [
    "instruction-following",
    "structured-output",
    "summarization",
    "reasoning"
   ],
   "name": "foundation",
   "path": "/Users/someone/.apogee/training/regimes/foundation.yaml",
   "promote_as": "",
   "source": "seeded",
   "student": "",
   "teacher": "",
   "temperature": 0.0
  },
  {
   "config": false,
   "count": 0,
   "description": "The harness chores as one gated pipeline -- the utility role's rewrites and summaries, the extraction role's schema-held JSON, then tool calls.",
   "iters": 0,
   "kits": [
    "utility-chores",
    "extraction-fidelity",
    "tool-calling"
   ],
   "name": "harness-helpers",
   "path": "/Users/someone/.apogee/training/regimes/harness-helpers.yaml",
   "promote_as": "",
   "source": "seeded",
   "student": "",
   "teacher": "",
   "temperature": 0.0
  },
  {
   "config": false,
   "count": 0,
   "description": "The knowledge-capture clerk's chore as one gated stage -- a raw discussion read into a canonical intent record.",
   "iters": 0,
   "kits": [
    "knowledge-capture"
   ],
   "name": "knowledge",
   "path": "/Users/someone/.apogee/training/regimes/knowledge.yaml",
   "promote_as": "",
   "source": "seeded",
   "student": "",
   "teacher": "",
   "temperature": 0.0
  }
 ],
 "object": "list",
 "problems": []
}
"""#
    /// tests/business/reads/Fixtures/train-run.err
    static let trainRun = #"""
[train] run 20261021-051826: mock lora on student, dataset data.jsonl
[train] mock trainer: 4 iteration(s), method lora
[train] iter 1/4 · loss 0.7200 · lr 1.00e-04 · 10.0 it/s
[train] iter 2/4 · loss 0.6200 · lr 1.00e-04 · 10.0 it/s
[train] iter 3/4 · loss 0.5200 · lr 1.00e-04 · 10.0 it/s
[train] iter 4/4 · loss 0.4200 · lr 1.00e-04 · 10.0 it/s
[train] adapter saved to /Users/someone/.apogee/training/runs/20261021-051826/adapters
"""#
    /// tests/business/reads/Fixtures/datasets-kits.json
    static let datasetsKits = #"""
{
 "data": [
  {
   "description": "The extraction role's chores in the product's own requests -- the graph clerk's entities and relations, and the shipped symphonies' fact, action and card stages -- each one JSON object held to its schema.",
   "eval_items": 24,
   "name": "extraction-fidelity",
   "path": "/Users/someone/.apogee/training/kits/extraction-fidelity.yaml"
  },
  {
   "description": "Follow explicit instructions precisely \u2014 format, length, and constraints.",
   "eval_items": 24,
   "name": "instruction-following",
   "path": "/Users/someone/.apogee/training/kits/instruction-following.yaml"
  },
  {
   "description": "The knowledge-capture clerk's chore in the product's own request -- a raw discussion read into one canonical intent record: the why, the decision, its status and discipline, and the link it names.",
   "eval_items": 24,
   "name": "knowledge-capture",
   "path": "/Users/someone/.apogee/training/kits/knowledge-capture.yaml"
  },
  {
   "description": "Solve arithmetic and logic problems with a correct final answer.",
   "eval_items": 24,
   "name": "reasoning",
   "path": "/Users/someone/.apogee/training/kits/reasoning.yaml"
  },
  {
   "description": "Emit well-formed JSON that matches a requested shape.",
   "eval_items": 24,
   "name": "structured-output",
   "path": "/Users/someone/.apogee/training/kits/structured-output.yaml"
  },
  {
   "description": "Condense a passage into a faithful, concise summary.",
   "eval_items": 24,
   "name": "summarization",
   "path": "/Users/someone/.apogee/training/kits/summarization.yaml"
  },
  {
   "description": "Choosing the one tool call a request needs -- the right tool and arguments its schema accepts -- against the product's own file, shell and git tool declarations.",
   "eval_items": 32,
   "name": "tool-calling",
   "path": "/Users/someone/.apogee/training/kits/tool-calling.yaml"
  },
  {
   "description": "The utility role's chores in the product's own formats -- a follow-up rewritten as a standalone search query, and a tool's output summarised for the model that acts on it.",
   "eval_items": 24,
   "name": "utility-chores",
   "path": "/Users/someone/.apogee/training/kits/utility-chores.yaml"
  }
 ],
 "object": "list"
}
"""#
    /// tests/business/reads/Fixtures/datasets-list.json
    static let datasetsList = #"""
{
 "data": [
  {
   "bytes": 449,
   "lines": 2,
   "name": "heat-notes",
   "path": "/Users/someone/.apogee/training/datasets/heat-notes.jsonl",
   "shape": "chat"
  }
 ],
 "object": "list"
}
"""#
    /// tests/business/reads/Fixtures/models-stored-list-all.json
    static let modelsListAll = #"""
{
 "data": [
  {
   "architecture": "Gemma4",
   "backend": "(not configured)",
   "backend_type": "-",
   "format": "safetensors",
   "group": "safetensors",
   "model": "Gemma4-26B-Instruct/safetensors/13de95725073",
   "profile": "-",
   "roles": "",
   "section": "local",
   "source": "huggingface",
   "state": "safetensors",
   "type": "model",
   "verified": "9 file(s) on record"
  },
  {
   "architecture": "Llama",
   "backend": "(not configured)",
   "backend_type": "-",
   "format": "safetensors",
   "group": "safetensors",
   "model": "Llama3.2-1B-Instruct/safetensors/edaba290ce7f",
   "profile": "-",
   "roles": "",
   "section": "local",
   "source": "huggingface",
   "state": "safetensors",
   "type": "model",
   "verified": "9 file(s) on record"
  },
  {
   "architecture": "Qwen3VL",
   "backend": "(not configured)",
   "backend_type": "-",
   "format": "safetensors",
   "group": "safetensors",
   "model": "Qwen3-VL-8B-Instruct/safetensors/63f2634a2019",
   "profile": "-",
   "roles": "",
   "section": "local",
   "source": "huggingface",
   "state": "safetensors",
   "type": "model",
   "verified": "14 file(s) on record"
  },
  {
   "architecture": "gemma4",
   "backend": "(not configured)",
   "backend_type": "-",
   "format": "mlx",
   "group": "mlx",
   "model": "Gemma4-26B-Instruct/mlx/a88b9f386aff",
   "note": "gemma4, mixed 4/8-bit (group 64), 32768-token window (the default; trained for 262144), 13.2 GiB",
   "profile": "gemma3",
   "quant": "mixed 4/8-bit (group 64)",
   "roles": "",
   "section": "local",
   "source": "converted",
   "state": "mlx",
   "type": "model",
   "verified": "10 file(s) on record",
   "window": "32768-token window (the default; trained for 262144)"
  },
  {
   "architecture": "WanPipeline",
   "backend": "(not configured)",
   "backend_type": "-",
   "bytes": 22690000547,
   "components": 5,
   "format": "pipeline",
   "group": "pipeline",
   "model": "Wan2.2-TI2V-5B/pipeline/9b1e47c0a2f6",
   "note": "WanPipeline, 5 components (scheduler, text_encoder, tokenizer, transformer, vae), 3 shard(s), 21.1 GiB",
   "profile": "-",
   "roles": "",
   "section": "local",
   "source": "local",
   "state": "pipeline",
   "type": "model",
   "verified": "no record"
  },
  {
   "architecture": "kokoro-v1_0.pth",
   "backend": "(not configured)",
   "backend_type": "-",
   "bytes": 1028,
   "format": "voice",
   "group": "voice",
   "model": "Kokoro-82M/voice/5f3a2c91d0e4",
   "note": "kokoro-v1_0.pth, 3 voices (American English, British English), 1 KiB",
   "profile": "-",
   "roles": "",
   "section": "local",
   "source": "local",
   "state": "voice",
   "type": "model",
   "verified": "no record"
  },
  {
   "architecture": "-",
   "backend": "mock-chat",
   "backend_type": "mock",
   "group": "other",
   "model": "",
   "profile": "unprofiled",
   "roles": "",
   "section": "local",
   "source": "-",
   "state": "-",
   "type": "model",
   "verified": "-"
  }
 ],
 "folded": 0,
 "object": "list"
}
"""#
    /// tests/business/reads/Fixtures/train-regime-list-bad-config.err
    static let regimesBadConfig = #"""
apogee train: /Users/someone/.apogee/config/config.json: not valid JSON: line 4, column 5: expected ',' or '}' after a member
"""#
    /// tests/business/reads/Fixtures/models-list-bad-config.err
    static let modelsBadConfig = #"""
apogee models: /Users/someone/.apogee/config/config.json: not valid JSON: line 4, column 5: expected ',' or '}' after a member
"""#
    /// tests/business/reads/Fixtures/train-status-passed.json
    static let trainStatusPassed = #"""
{"active_pipeline":null,"cycle":null,"cycle_active":false,"latest_pipeline":null,"pipelines":0,"recent_runs":[{"base_model":"/Users/someone/.apogee/models/tiny/safetensors/aaaaaaaaaaaa","dataset":"/Users/someone/.apogee/training/datasets/starter.jsonl","eval_passed":true,"eval_score":1.0,"final_loss":0.42,"finished_at":"2026-10-26T06:56:37Z","id":"20261026-065637","iterations":2,"kind":"run","method":"lora","started_at":"2026-10-26T06:56:37Z","status":"complete","trainer":"mock"}],"running":[],"runs":1,"versions":[]}
"""#
    /// tests/business/reads/Fixtures/train-versions-adapters.json
    static let trainVersionsAdapters = #"""
{"data":[{"active_version":1,"backend_name":"based","versions":[{"adapter_path":"/Users/someone/.apogee/training/versions/based/v1.lora.gguf","base_sha256":"ce5065b7f3dc1bcd98592fa57a11f0e9670f16f33f21abf043dd7c903ad0c98c","eval_passed":true,"eval_score":1.0,"promoted_at":"2026-10-26T06:56:37Z","run_id":"20261026-065637","version":1}]},{"active_version":1,"backend_name":"q4","versions":[{"adapter_path":"/Users/someone/.apogee/training/versions/q4/v1.lora.gguf","base_sha256":"a81cdd0551b6e79be2276ac019df1c98656527cbdc1829fc7fbf097253c31546","eval_passed":true,"eval_score":1.0,"eval_scored_on":"tiny-Q4_K_M.gguf (Q4_K_M)","promoted_at":"2026-10-26T06:56:39Z","run_id":"20261026-065638","version":1}]},{"active_version":1,"backend_name":"tuned","versions":[{"eval_passed":true,"eval_score":1.0,"gguf_path":"/Users/someone/.apogee/models/tiny/gguf/cf368ccf9b45/tuned-v1.gguf","promoted_at":"2026-10-26T06:56:37Z","run_id":"20261026-065637","version":1}]}],"object":"list"}
"""#
    /// tests/business/reads/Fixtures/models-list-adapters.json
    static let modelsListAdapters = #"""
{"data":[{"architecture":"Llama","backend":"(not configured)","backend_type":"-","format":"safetensors","group":"safetensors","model":"tiny/safetensors/aaaaaaaaaaaa","profile":"-","roles":"","section":"local","source":"local","state":"safetensors","type":"model","verified":"no record"},{"architecture":"-","backend":"based","backend_type":"llamacpp","format":"gguf","group":"gguf","model":"tiny-F16.gguf","note":"header runs past the end of the file (truncated or corrupt)","profile":"unprofiled","roles":"","section":"local","source":"converted","state":"unreadable","type":"model","verified":"no record"},{"architecture":"llama","backend":"q4","backend_type":"llamacpp","format":"gguf","group":"gguf","model":"tiny-Q4_K_M.gguf","profile":"llama3 (unverified)","roles":"","section":"local","source":"converted","state":"ok","type":"model","verified":"no record"},{"architecture":"llama","backend":"tuned","backend_type":"llamacpp","format":"gguf","group":"gguf","model":"tuned-v1.gguf","profile":"llama3 (unverified)","roles":"","section":"local","source":"local","state":"ok","type":"model","verified":"size ok, no digest published, header ok"}],"folded":0,"object":"list"}
"""#
    /// tests/business/reads/Fixtures/train-promote-names-adapter.err
    static let promoteNamesAdapter = #"""
apogee train: 'based' holds adapter versions (active v1) applied over its base, and a merged version would replace that base under them -- promote into it with --adapter, or merged under a new name
"""#
    /// tests/business/reads/Fixtures/train-promote-adapter.err
    static let promoteAdapterSaid = #"""
[promote] run 20261026-065637 -> based v1 (adapter)
[promote] no fuse: the adapter is the version, applied over the entry's base when it loads
[promote]   trained on: /Users/someone/.apogee/models/tiny/safetensors/aaaaaaaaaaaa
[promote]   adapter:    /Users/someone/.apogee/training/runs/20261026-065637/adapters
[promote] converting the adapter to a GGUF LoRA (F16): /Users/someone/.apogee/training/versions/based/v1.lora.gguf
[promote] mock converter: writing a minimal GGUF LoRA
[promote] pinning the adapter to the base it applies over: /Users/someone/.apogee/models/tiny/gguf/bbbbbbbbbbbb/tiny-F16.gguf
"""#
    /// tests/business/reads/Fixtures/train-promote-adapter-no-suite.err
    static let promoteAdapterNoSuite = #"""
apogee train: no eval suite to score run 20261026-065638 on tiny-Q4_K_M.gguf (Q4_K_M), the base 'q4' serves -- pass --suite <path|name>, or set training.eval_suite_path, or pass --force to skip the gate
"""#
    /// tests/business/reads/Fixtures/datasets-pull-handmade.txt
    static let pullDone = #"""
downloading 2 file(s), 3 MiB, into /Users/someone/.apogee/training/datasets/raw/someone--heat-notes
  [1/2] data/train.jsonl
  [2/2] data/test.jsonl

/Users/someone/.apogee/training/datasets/raw/someone--heat-notes
2 file(s), 3 MiB

Convert it with:  apogee datasets prepare /Users/someone/.apogee/training/datasets/raw/someone--heat-notes
"""#
    /// tests/business/reads/Fixtures/datasets-pull-bad-ref.err
    static let pullBadRef = #"""
apogee datasets: 'nonsense' is not a Hugging Face dataset ref (expected owner/name)
"""#
    /// tests/business/reads/Fixtures/train-pipeline-complete.txt
    static let pipelineComplete = #"""
nightly
skills
"""#
    /// tests/business/reads/Fixtures/train-pipeline-complete-none.txt
    static let pipelineCompleteNone = #"""

"""#
    /// tests/business/reads/Fixtures/train-pipeline-status-none.json
    static let pipelineStatusNone = #"""
{"data":[],"object":"list"}
"""#
    /// tests/business/reads/Fixtures/train-pipeline-status-list.json
    static let pipelineStatusList = #"""
{"data":[{"id":"pipe-20261026-074117","kind":"pipeline","passed":1,"spec_name":"skills","stages":2,"started_at":"2026-10-26T07:41:17Z","status":"aborted"},{"completed_at":"2026-10-26T07:41:16Z","id":"pipe-20261026-074116","kind":"pipeline","passed":2,"spec_name":"skills","stages":2,"started_at":"2026-10-26T07:41:16Z","status":"complete"},{"completed_at":"2026-10-26T07:41:15Z","id":"pipe-20261026-074115","kind":"pipeline","passed":1,"spec_name":"nightly","stages":1,"started_at":"2026-10-26T07:41:15Z","status":"complete"},{"completed_at":"2026-10-26T07:41:13Z","id":"pipe-20261026-074112","kind":"pipeline","passed":2,"spec_name":"skills","stages":2,"started_at":"2026-10-26T07:41:12Z","status":"complete"}],"object":"list"}
"""#
    /// tests/business/reads/Fixtures/train-pipeline-status-aborted-list.json
    static let pipelineStatusAbortedList = #"""
{"data":[{"id":"pipe-20261026-074112","kind":"pipeline","passed":1,"spec_name":"skills","stages":2,"started_at":"2026-10-26T07:41:12Z","status":"aborted"}],"object":"list"}
"""#
    /// tests/business/reads/Fixtures/train-pipeline-status-aborted.json
    static let pipelineStatusAborted = #"""
{"kind":"pipeline","pipeline":{"base_model":"/Users/someone/.apogee/models/tiny/safetensors/aaaaaaaaaaaa","pipeline_run_id":"pipe-20261026-074112","spec_name":"skills","stages":[{"adapter_dir":"/Users/someone/.apogee/training/runs/pipe-20261026-074112-s0/adapters","base_model":"/Users/someone/.apogee/models/tiny/safetensors/aaaaaaaaaaaa","cumulative_passed":true,"cumulative_score":1.0,"dataset":"/Users/someone/.apogee/training/datasets/starter.jsonl","eval_results":{"items":[{"check_type":"contains","expected":"hello","got":"say hello","passed":true,"prompt":"say hello"}],"num_passed":1,"num_skipped":0,"passed":true,"run_at":"2026-10-26T07:41:12Z","score":1.0,"suite":"/Users/someone/.apogee/training/suites/hello.jsonl","total":1},"fused_dir":"/Users/someone/.apogee/training/runs/pipe-20261026-074112-s0/fused","index":0,"name":"greetings","run_id":"pipe-20261026-074112-s0","status":"passed"},{"adapter_dir":"/Users/someone/.apogee/training/runs/pipe-20261026-074112-s1/adapters","base_model":"/Users/someone/.apogee/training/runs/pipe-20261026-074112-s0/fused","cumulative_passed":false,"cumulative_score":0.5,"dataset":"/Users/someone/.apogee/training/datasets/regress.jsonl","eval_results":{"items":[{"check_type":"contains","expected":"hello","got":"nope","passed":false,"prompt":"say hello"},{"check_type":"contains","expected":"nope","got":"nope","passed":true,"prompt":"say nope"}],"num_passed":1,"num_skipped":0,"passed":false,"run_at":"2026-10-26T07:41:12Z","score":0.5,"suite":"/Users/someone/.apogee/training/suites/hello.jsonl + /Users/someone/.apogee/training/suites/nope.jsonl","total":2},"fused_dir":"","index":1,"name":"refusals","run_id":"pipe-20261026-074112-s1","status":"failed"}],"started_at":"2026-10-26T07:41:12Z","status":"aborted","student":"tiny"}}
"""#
    /// tests/business/reads/Fixtures/train-pipeline-status-complete.json
    static let pipelineStatusComplete = #"""
{"kind":"pipeline","pipeline":{"base_model":"/Users/someone/.apogee/models/tiny/safetensors/aaaaaaaaaaaa","completed_at":"2026-10-26T07:41:13Z","pipeline_run_id":"pipe-20261026-074112","spec_name":"skills","stages":[{"adapter_dir":"/Users/someone/.apogee/training/runs/pipe-20261026-074112-s0/adapters","base_model":"/Users/someone/.apogee/models/tiny/safetensors/aaaaaaaaaaaa","cumulative_passed":true,"cumulative_score":1.0,"dataset":"/Users/someone/.apogee/training/datasets/starter.jsonl","eval_results":{"items":[{"check_type":"contains","expected":"hello","got":"say hello","passed":true,"prompt":"say hello"}],"num_passed":1,"num_skipped":0,"passed":true,"run_at":"2026-10-26T07:41:12Z","score":1.0,"suite":"/Users/someone/.apogee/training/suites/hello.jsonl","total":1},"fused_dir":"/Users/someone/.apogee/training/runs/pipe-20261026-074112-s0/fused","index":0,"name":"greetings","run_id":"pipe-20261026-074112-s0","status":"passed"},{"adapter_dir":"/Users/someone/.apogee/training/runs/pipe-20261026-074112-s1/adapters","base_model":"/Users/someone/.apogee/training/runs/pipe-20261026-074112-s0/fused","cumulative_passed":true,"cumulative_score":1.0,"dataset":"/Users/someone/.apogee/training/datasets/regress.jsonl","eval_results":{"items":[{"check_type":"contains","expected":"hello","got":"say hello","passed":true,"prompt":"say hello"},{"check_type":"contains","expected":"nope","got":"say nope","passed":true,"prompt":"say nope"}],"num_passed":2,"num_skipped":0,"passed":true,"run_at":"2026-10-26T07:41:13Z","score":1.0,"suite":"/Users/someone/.apogee/training/suites/hello.jsonl + /Users/someone/.apogee/training/suites/nope.jsonl","total":2},"fused_dir":"","index":1,"name":"refusals","run_id":"pipe-20261026-074112-s1","status":"passed"}],"started_at":"2026-10-26T07:41:12Z","status":"complete","student":"tiny"}}
"""#
    /// tests/business/jobs/Fixtures/train-pipeline-run.jsonl
    static let pipelineRun = #"""
{"protocol_version":1,"schema":"2026-10-25","type":"job","verb":"train pipeline run"}
{"text":"[pipeline] skills as pipe-20261026-074112: 2 stage(s), mock on aaaaaaaaaaaa; no judge: unmatched items skip","type":"notice"}
{"count":2,"index":1,"label":"greetings","pipeline_run_id":"pipe-20261026-074112","state":"pending","type":"stage"}
{"count":2,"index":2,"label":"refusals","pipeline_run_id":"pipe-20261026-074112","state":"pending","type":"stage"}
{"count":2,"index":1,"label":"greetings","pipeline_run_id":"pipe-20261026-074112","run_id":"pipe-20261026-074112-s0","state":"training","type":"stage"}
{"text":"[train] stage 1/2 'greetings': mock lora on aaaaaaaaaaaa, dataset starter.jsonl","type":"notice"}
{"text":"[train] mock trainer: 4 iteration(s), method lora","type":"notice"}
{"iteration":1,"iterations":4,"loss":0.72,"lr":0.0001,"stage":1,"throughput":10.0,"type":"iteration"}
{"iteration":2,"iterations":4,"loss":0.62,"lr":0.0001,"stage":1,"throughput":10.0,"type":"iteration"}
{"iteration":3,"iterations":4,"loss":0.52,"lr":0.0001,"stage":1,"throughput":10.0,"type":"iteration"}
{"iteration":4,"iterations":4,"loss":0.42,"lr":0.0001,"stage":1,"throughput":10.0,"type":"iteration"}
{"text":"[train] adapter saved to /Users/someone/.apogee/training/runs/pipe-20261026-074112-s0/adapters","type":"notice"}
{"count":2,"index":1,"label":"greetings","pipeline_run_id":"pipe-20261026-074112","run_id":"pipe-20261026-074112-s0","state":"evaluating","type":"stage"}
{"text":"[eval] cumulative suite: 1 suite(s), 1 item(s); no judge: unmatched items skip","type":"notice"}
{"text":"[gate] stage 'greetings' PASSED the cumulative eval -- 100% (1/1)","type":"notice"}
{"count":2,"cumulative_score":1.0,"index":1,"label":"greetings","pipeline_run_id":"pipe-20261026-074112","run_id":"pipe-20261026-074112-s0","state":"fusing","type":"stage"}
{"text":"[fuse] merging stage 1's adapter into its base for stage 2","type":"notice"}
{"text":"[fuse] mock fuse: /Users/someone/.apogee/training/runs/pipe-20261026-074112-s0/adapters into /Users/someone/.apogee/models/tiny/safetensors/aaaaaaaaaaaa","type":"notice"}
{"count":2,"cumulative_score":1.0,"index":1,"label":"greetings","pipeline_run_id":"pipe-20261026-074112","run_id":"pipe-20261026-074112-s0","state":"passed","type":"stage"}
{"count":2,"index":2,"label":"refusals","pipeline_run_id":"pipe-20261026-074112","run_id":"pipe-20261026-074112-s1","state":"training","type":"stage"}
{"text":"[train] stage 2/2 'refusals': mock lora on fused, dataset regress.jsonl","type":"notice"}
{"text":"[train] mock trainer: 4 iteration(s), method lora","type":"notice"}
{"iteration":1,"iterations":4,"loss":0.72,"lr":0.0001,"stage":2,"throughput":10.0,"type":"iteration"}
{"iteration":2,"iterations":4,"loss":0.62,"lr":0.0001,"stage":2,"throughput":10.0,"type":"iteration"}
{"iteration":3,"iterations":4,"loss":0.52,"lr":0.0001,"stage":2,"throughput":10.0,"type":"iteration"}
{"iteration":4,"iterations":4,"loss":0.42,"lr":0.0001,"stage":2,"throughput":10.0,"type":"iteration"}
{"text":"[train] adapter saved to /Users/someone/.apogee/training/runs/pipe-20261026-074112-s1/adapters","type":"notice"}
{"count":2,"index":2,"label":"refusals","pipeline_run_id":"pipe-20261026-074112","run_id":"pipe-20261026-074112-s1","state":"evaluating","type":"stage"}
{"text":"[eval] cumulative suite: 2 suite(s), 2 item(s); no judge: unmatched items skip","type":"notice"}
{"text":"[gate] stage 'refusals' FAILED the cumulative eval -- 50% (1/2)","type":"notice"}
{"count":2,"cumulative_score":0.5,"index":2,"label":"refusals","pipeline_run_id":"pipe-20261026-074112","run_id":"pipe-20261026-074112-s1","state":"failed","type":"stage"}
{"document":{"kind":"pipeline","pipeline":{"base_model":"/Users/someone/.apogee/models/tiny/safetensors/aaaaaaaaaaaa","pipeline_run_id":"pipe-20261026-074112","spec_name":"skills","stages":[{"adapter_dir":"/Users/someone/.apogee/training/runs/pipe-20261026-074112-s0/adapters","base_model":"/Users/someone/.apogee/models/tiny/safetensors/aaaaaaaaaaaa","cumulative_passed":true,"cumulative_score":1.0,"dataset":"/Users/someone/.apogee/training/datasets/starter.jsonl","eval_results":{"items":[{"check_type":"contains","expected":"hello","got":"say hello","passed":true,"prompt":"say hello"}],"num_passed":1,"num_skipped":0,"passed":true,"run_at":"2026-10-26T07:41:12Z","score":1.0,"suite":"/Users/someone/.apogee/training/suites/hello.jsonl","total":1},"fused_dir":"/Users/someone/.apogee/training/runs/pipe-20261026-074112-s0/fused","index":0,"name":"greetings","run_id":"pipe-20261026-074112-s0","status":"passed"},{"adapter_dir":"/Users/someone/.apogee/training/runs/pipe-20261026-074112-s1/adapters","base_model":"/Users/someone/.apogee/training/runs/pipe-20261026-074112-s0/fused","cumulative_passed":false,"cumulative_score":0.5,"dataset":"/Users/someone/.apogee/training/datasets/regress.jsonl","eval_results":{"items":[{"check_type":"contains","expected":"hello","got":"nope","passed":false,"prompt":"say hello"},{"check_type":"contains","expected":"nope","got":"nope","passed":true,"prompt":"say nope"}],"num_passed":1,"num_skipped":0,"passed":false,"run_at":"2026-10-26T07:41:12Z","score":0.5,"suite":"/Users/someone/.apogee/training/suites/hello.jsonl + /Users/someone/.apogee/training/suites/nope.jsonl","total":2},"fused_dir":"","index":1,"name":"refusals","run_id":"pipe-20261026-074112-s1","status":"failed"}],"started_at":"2026-10-26T07:41:12Z","status":"aborted","student":"tiny"}},"type":"document"}
{"message":"pipeline aborted -- stage 'refusals' failed the cumulative eval gate (50% (1/2)). Fix the data or the suite and 'apogee train pipeline resume pipe-20261026-074112', or pass --continue-on-fail to go on regardless","type":"error"}
"""#
    /// tests/business/reads/Fixtures/train-status-none.json
    static let trainStatusNone = #"""
{"active_pipeline":null,"cycle":null,"cycle_active":false,"latest_pipeline":null,"pipelines":0,"recent_runs":[],"running":[],"runs":0,"versions":[]}
"""#
    /// tests/business/reads/Fixtures/train-status-cycle.json
    static let trainStatusCycle = #"""
{"active_pipeline":null,"cycle":{"anchor_score":1.0,"anchor_version":1,"backend":"nightly-model","consecutive_fails":0,"halt_reason":"","halted":false,"total_runs":2},"cycle_active":false,"latest_pipeline":{"completed_at":"2026-10-26T07:41:19Z","id":"cycle-20261026-074119","kind":"pipeline","passed":1,"spec_name":"nightly","stages":1,"started_at":"2026-10-26T07:41:19Z","status":"complete"},"pipelines":5,"recent_runs":[{"base_model":"/Users/someone/.apogee/training/runs/pipe-20261026-074117-s0/fused","dataset":"/Users/someone/.apogee/training/datasets/regress.jsonl","final_loss":0.0,"finished_at":"2026-10-26T07:41:17Z","id":"pipe-20261026-074117-s1","iterations":0,"kind":"run","method":"lora","started_at":"2026-10-26T07:41:17Z","status":"cancelled","trainer":"mock"},{"base_model":"/Users/someone/.apogee/models/tiny/safetensors/aaaaaaaaaaaa","dataset":"/Users/someone/.apogee/training/datasets/starter.jsonl","eval_passed":true,"eval_score":1.0,"final_loss":0.42,"finished_at":"2026-10-26T07:41:17Z","id":"pipe-20261026-074117-s0","iterations":4,"kind":"run","method":"lora","started_at":"2026-10-26T07:41:17Z","status":"complete","trainer":"mock"},{"base_model":"/Users/someone/.apogee/training/runs/pipe-20261026-074116-s0/fused","dataset":"/Users/someone/.apogee/training/datasets/regress.jsonl","eval_passed":true,"eval_score":1.0,"final_loss":0.42,"finished_at":"2026-10-26T07:41:16Z","id":"pipe-20261026-074116-s1","iterations":3000,"kind":"run","method":"lora","started_at":"2026-10-26T07:41:16Z","status":"complete","trainer":"mock"},{"base_model":"/Users/someone/.apogee/models/tiny/safetensors/aaaaaaaaaaaa","dataset":"/Users/someone/.apogee/training/datasets/starter.jsonl","eval_passed":true,"eval_score":1.0,"final_loss":0.42,"finished_at":"2026-10-26T07:41:16Z","id":"pipe-20261026-074116-s0","iterations":4,"kind":"run","method":"lora","started_at":"2026-10-26T07:41:16Z","status":"complete","trainer":"mock"},{"base_model":"/Users/someone/.apogee/models/tiny/safetensors/aaaaaaaaaaaa","dataset":"/Users/someone/.apogee/training/datasets/starter.jsonl","eval_passed":true,"eval_score":1.0,"final_loss":0.42,"finished_at":"2026-10-26T07:41:15Z","id":"pipe-20261026-074115-s0","iterations":4,"kind":"run","method":"lora","started_at":"2026-10-26T07:41:15Z","status":"complete","trainer":"mock"}],"running":[],"runs":8,"versions":[{"active_version":1,"backend":"nightly-model","kept":1,"total":1}]}
"""#
    /// tests/business/reads/Fixtures/train-status-cycle-halted.json
    static let trainStatusCycleHalted = #"""
{"active_pipeline":null,"cycle":{"anchor_score":1.0,"anchor_version":1,"backend":"nightly-model","consecutive_fails":1,"halt_reason":"circuit breaker: 1 consecutive failed cycle(s) reached circuit_breaker_k 1","halted":true,"total_runs":3},"cycle_active":false,"latest_pipeline":{"id":"cycle-20261026-074120","kind":"pipeline","passed":0,"spec_name":"nightly","stages":1,"started_at":"2026-10-26T07:41:20Z","status":"aborted"},"pipelines":6,"recent_runs":[{"base_model":"/Users/someone/.apogee/training/runs/pipe-20261026-074117-s0/fused","dataset":"/Users/someone/.apogee/training/datasets/regress.jsonl","final_loss":0.0,"finished_at":"2026-10-26T07:41:17Z","id":"pipe-20261026-074117-s1","iterations":0,"kind":"run","method":"lora","started_at":"2026-10-26T07:41:17Z","status":"cancelled","trainer":"mock"},{"base_model":"/Users/someone/.apogee/models/tiny/safetensors/aaaaaaaaaaaa","dataset":"/Users/someone/.apogee/training/datasets/starter.jsonl","eval_passed":true,"eval_score":1.0,"final_loss":0.42,"finished_at":"2026-10-26T07:41:17Z","id":"pipe-20261026-074117-s0","iterations":4,"kind":"run","method":"lora","started_at":"2026-10-26T07:41:17Z","status":"complete","trainer":"mock"},{"base_model":"/Users/someone/.apogee/training/runs/pipe-20261026-074116-s0/fused","dataset":"/Users/someone/.apogee/training/datasets/regress.jsonl","eval_passed":true,"eval_score":1.0,"final_loss":0.42,"finished_at":"2026-10-26T07:41:16Z","id":"pipe-20261026-074116-s1","iterations":3000,"kind":"run","method":"lora","started_at":"2026-10-26T07:41:16Z","status":"complete","trainer":"mock"},{"base_model":"/Users/someone/.apogee/models/tiny/safetensors/aaaaaaaaaaaa","dataset":"/Users/someone/.apogee/training/datasets/starter.jsonl","eval_passed":true,"eval_score":1.0,"final_loss":0.42,"finished_at":"2026-10-26T07:41:16Z","id":"pipe-20261026-074116-s0","iterations":4,"kind":"run","method":"lora","started_at":"2026-10-26T07:41:16Z","status":"complete","trainer":"mock"},{"base_model":"/Users/someone/.apogee/models/tiny/safetensors/aaaaaaaaaaaa","dataset":"/Users/someone/.apogee/training/datasets/starter.jsonl","eval_passed":true,"eval_score":1.0,"final_loss":0.42,"finished_at":"2026-10-26T07:41:15Z","id":"pipe-20261026-074115-s0","iterations":4,"kind":"run","method":"lora","started_at":"2026-10-26T07:41:15Z","status":"complete","trainer":"mock"}],"running":[],"runs":9,"versions":[{"active_version":1,"backend":"nightly-model","kept":1,"total":1}]}
"""#
    /// tests/business/reads/Fixtures/train-status-cycle-active.json
    static let trainStatusCycleActive = #"""
{"active_pipeline":"cycle-20261026-074121","cycle":{"anchor_score":1.0,"anchor_version":1,"backend":"nightly-model","consecutive_fails":0,"halt_reason":"","halted":false,"total_runs":3},"cycle_active":true,"latest_pipeline":{"id":"cycle-20261026-074121","kind":"pipeline","passed":0,"spec_name":"nightly","stages":1,"started_at":"2026-10-26T07:41:21Z","status":"running"},"pipelines":7,"recent_runs":[{"base_model":"/Users/someone/.apogee/training/runs/pipe-20261026-074117-s0/fused","dataset":"/Users/someone/.apogee/training/datasets/regress.jsonl","final_loss":0.0,"finished_at":"2026-10-26T07:41:17Z","id":"pipe-20261026-074117-s1","iterations":0,"kind":"run","method":"lora","started_at":"2026-10-26T07:41:17Z","status":"cancelled","trainer":"mock"},{"base_model":"/Users/someone/.apogee/models/tiny/safetensors/aaaaaaaaaaaa","dataset":"/Users/someone/.apogee/training/datasets/starter.jsonl","eval_passed":true,"eval_score":1.0,"final_loss":0.42,"finished_at":"2026-10-26T07:41:17Z","id":"pipe-20261026-074117-s0","iterations":4,"kind":"run","method":"lora","started_at":"2026-10-26T07:41:17Z","status":"complete","trainer":"mock"},{"base_model":"/Users/someone/.apogee/training/runs/pipe-20261026-074116-s0/fused","dataset":"/Users/someone/.apogee/training/datasets/regress.jsonl","eval_passed":true,"eval_score":1.0,"final_loss":0.42,"finished_at":"2026-10-26T07:41:16Z","id":"pipe-20261026-074116-s1","iterations":3000,"kind":"run","method":"lora","started_at":"2026-10-26T07:41:16Z","status":"complete","trainer":"mock"},{"base_model":"/Users/someone/.apogee/models/tiny/safetensors/aaaaaaaaaaaa","dataset":"/Users/someone/.apogee/training/datasets/starter.jsonl","eval_passed":true,"eval_score":1.0,"final_loss":0.42,"finished_at":"2026-10-26T07:41:16Z","id":"pipe-20261026-074116-s0","iterations":4,"kind":"run","method":"lora","started_at":"2026-10-26T07:41:16Z","status":"complete","trainer":"mock"},{"base_model":"/Users/someone/.apogee/models/tiny/safetensors/aaaaaaaaaaaa","dataset":"/Users/someone/.apogee/training/datasets/starter.jsonl","eval_passed":true,"eval_score":1.0,"final_loss":0.42,"finished_at":"2026-10-26T07:41:15Z","id":"pipe-20261026-074115-s0","iterations":4,"kind":"run","method":"lora","started_at":"2026-10-26T07:41:15Z","status":"complete","trainer":"mock"}],"running":["cycle-20261026-074121-s0"],"runs":10,"versions":[{"active_version":1,"backend":"nightly-model","kept":1,"total":1}]}
"""#
    /// tests/business/reads/Fixtures/train-cycle-status-passed.json
    static let cycleStatusPassed = #"""
{"active":false,"anchor_score":1.0,"anchor_version":1,"backend":"nightly-model","consecutive_fails":0,"halt_reason":"","halted":false,"runs":[{"anchor_score":0.0,"cycle_score":0.0,"dataset_rows":0,"gate":"skipped","gate_reason":"no new training data in any source","pipeline_id":"","prev_score":0.0,"promoted_version":0,"run_at":"2026-10-26T07:41:18Z","source":""},{"anchor_score":0.0,"cycle_score":1.0,"dataset_rows":2,"gate":"pass","gate_reason":"","pipeline_id":"cycle-20261026-074119","prev_score":0.0,"promoted_version":1,"run_at":"2026-10-26T07:41:19Z","source":"directory"}],"sessions_until":"","total_runs":2}
"""#
    /// tests/business/reads/Fixtures/train-cycle-status-halted.json
    static let cycleStatusHalted = #"""
{"active":false,"anchor_score":1.0,"anchor_version":1,"backend":"nightly-model","consecutive_fails":1,"halt_reason":"circuit breaker: 1 consecutive failed cycle(s) reached circuit_breaker_k 1","halted":true,"runs":[{"anchor_score":0.0,"cycle_score":0.0,"dataset_rows":0,"gate":"skipped","gate_reason":"no new training data in any source","pipeline_id":"","prev_score":0.0,"promoted_version":0,"run_at":"2026-10-26T07:41:18Z","source":""},{"anchor_score":0.0,"cycle_score":1.0,"dataset_rows":2,"gate":"pass","gate_reason":"","pipeline_id":"cycle-20261026-074119","prev_score":0.0,"promoted_version":1,"run_at":"2026-10-26T07:41:19Z","source":"directory"},{"anchor_score":0.0,"cycle_score":0.0,"dataset_rows":1,"gate":"fail","gate_reason":"pipeline aborted: pipeline aborted -- stage 'base' failed the cumulative eval gate (0% (0/1)). Fix the data or the suite and 'apogee train pipeline resume cycle-20261026-074120', or pass --continue-on-fail to go on regardless","note":"circuit breaker: 1 consecutive failed cycle(s) reached circuit_breaker_k 1","pipeline_id":"cycle-20261026-074120","prev_score":0.0,"promoted_version":0,"run_at":"2026-10-26T07:41:20Z","source":"directory"}],"sessions_until":"","total_runs":3}
"""#
    /// tests/business/reads/Fixtures/train-cycle-status-active.json
    static let cycleStatusActive = #"""
{"active":true,"anchor_score":1.0,"anchor_version":1,"backend":"nightly-model","consecutive_fails":0,"halt_reason":"","halted":false,"runs":[{"anchor_score":0.0,"cycle_score":0.0,"dataset_rows":0,"gate":"skipped","gate_reason":"no new training data in any source","pipeline_id":"","prev_score":0.0,"promoted_version":0,"run_at":"2026-10-26T07:41:18Z","source":""},{"anchor_score":0.0,"cycle_score":1.0,"dataset_rows":2,"gate":"pass","gate_reason":"","pipeline_id":"cycle-20261026-074119","prev_score":0.0,"promoted_version":1,"run_at":"2026-10-26T07:41:19Z","source":"directory"},{"anchor_score":0.0,"cycle_score":0.0,"dataset_rows":1,"gate":"fail","gate_reason":"pipeline aborted: pipeline aborted -- stage 'base' failed the cumulative eval gate (0% (0/1)). Fix the data or the suite and 'apogee train pipeline resume cycle-20261026-074120', or pass --continue-on-fail to go on regardless","note":"circuit breaker: 1 consecutive failed cycle(s) reached circuit_breaker_k 1","pipeline_id":"cycle-20261026-074120","prev_score":0.0,"promoted_version":0,"run_at":"2026-10-26T07:41:20Z","source":"directory"}],"sessions_until":"","total_runs":3}
"""#
    /// tests/business/jobs/Fixtures/train-cycle-run-passed.jsonl
    static let cycleRunPassed = #"""
{"protocol_version":1,"schema":"2026-10-25","type":"job","verb":"train cycle run"}
{"state":"collecting","text":"[cycle] directory: 1 file(s) in /Users/someone/work/queue","type":"pass"}
{"state":"merging","text":"[cycle] 2 example(s) from directory","type":"pass"}
{"state":"training","text":"[cycle] pipeline cycle-20261026-074119 (nightly): 1 stage(s)","type":"pass"}
{"count":1,"index":1,"label":"base","pipeline_run_id":"cycle-20261026-074119","state":"pending","type":"stage"}
{"count":1,"index":1,"label":"base","pipeline_run_id":"cycle-20261026-074119","run_id":"cycle-20261026-074119-s0","state":"training","type":"stage"}
{"text":"[train] stage 1/1 'base': mock lora on aaaaaaaaaaaa, dataset merged.jsonl","type":"notice"}
{"text":"[train] mock trainer: 4 iteration(s), method lora","type":"notice"}
{"iteration":1,"iterations":4,"loss":0.72,"lr":0.0001,"stage":1,"throughput":10.0,"type":"iteration"}
{"iteration":2,"iterations":4,"loss":0.62,"lr":0.0001,"stage":1,"throughput":10.0,"type":"iteration"}
{"iteration":3,"iterations":4,"loss":0.52,"lr":0.0001,"stage":1,"throughput":10.0,"type":"iteration"}
{"iteration":4,"iterations":4,"loss":0.42,"lr":0.0001,"stage":1,"throughput":10.0,"type":"iteration"}
{"text":"[train] adapter saved to /Users/someone/.apogee/training/runs/cycle-20261026-074119-s0/adapters","type":"notice"}
{"count":1,"index":1,"label":"base","pipeline_run_id":"cycle-20261026-074119","run_id":"cycle-20261026-074119-s0","state":"evaluating","type":"stage"}
{"text":"[eval] cumulative suite: 1 suite(s), 1 item(s); no judge: unmatched items skip","type":"notice"}
{"text":"[gate] stage 'base' PASSED the cumulative eval -- 100% (1/1)","type":"notice"}
{"count":1,"cumulative_score":1.0,"index":1,"label":"base","pipeline_run_id":"cycle-20261026-074119","run_id":"cycle-20261026-074119-s0","state":"passed","type":"stage"}
{"state":"gating","text":"[cycle] gate -- current 100%, previous 0%, anchor 0%, threshold 0%","type":"pass"}
{"state":"promoting","text":"[cycle] gate PASSED (100%) -- promoting cycle-20261026-074119-s0 into nightly-model","type":"pass"}
{"text":"[promote] run cycle-20261026-074119-s0 -> nightly-model v1","type":"notice"}
{"text":"[promote] fusing the adapter into the base model","type":"notice"}
{"text":"[promote]   base:    /Users/someone/.apogee/models/tiny/safetensors/aaaaaaaaaaaa","type":"notice"}
{"text":"[promote]   adapter: /Users/someone/.apogee/training/runs/cycle-20261026-074119-s0/adapters","type":"notice"}
{"text":"[promote]   output:  /Users/someone/.apogee/training/runs/cycle-20261026-074119-s0/fused","type":"notice"}
{"text":"[promote] mock fuse: /Users/someone/.apogee/training/runs/cycle-20261026-074119-s0/adapters into /Users/someone/.apogee/models/tiny/safetensors/aaaaaaaaaaaa","type":"notice"}
{"text":"[promote] converting the fused model to GGUF (F16): /Users/someone/.apogee/models/tiny/gguf/.incoming-1c387267417d/nightly-model-v1.gguf","type":"notice"}
{"text":"[promote] mock converter: writing a minimal GGUF","type":"notice"}
{"state":"passed","text":"[cycle] anchor set to v1 (100%)","type":"pass"}
{"state":"passed","text":"[cycle] complete -- nightly-model promoted to v1 (/Users/someone/.apogee/models/tiny/gguf/052b295c3e9b/nightly-model-v1.gguf)","type":"pass"}
{"document":{"active":false,"anchor_score":1.0,"anchor_version":1,"backend":"nightly-model","consecutive_fails":0,"halt_reason":"","halted":false,"runs":[{"anchor_score":0.0,"cycle_score":0.0,"dataset_rows":0,"gate":"skipped","gate_reason":"no new training data in any source","pipeline_id":"","prev_score":0.0,"promoted_version":0,"run_at":"2026-10-26T07:41:18Z","source":""},{"anchor_score":0.0,"cycle_score":1.0,"dataset_rows":2,"gate":"pass","gate_reason":"","pipeline_id":"cycle-20261026-074119","prev_score":0.0,"promoted_version":1,"run_at":"2026-10-26T07:41:19Z","source":"directory"}],"sessions_until":"","total_runs":2},"type":"document"}
"""#
    /// tests/business/reads/Fixtures/store-list-kinds-registered.json
    static let kindsRegistered = #"""
{"data":[{"architecture":"gemma4_assistant","backend":"(not configured)","backend_type":"-","format":"mlx","group":"mlx","kind":"drafter","model":"Gemma4-E4B-Assistant/mlx/4d5e6f708192","note":"a drafter: it runs beside its base model, never alone","profile":"gemma3","quant":"bf16","roles":"","section":"local","source":"converted","state":"mlx","type":"model","verified":"5 file(s) on record","window":"32768-token window (the default)"},{"architecture":"kokoro","backend":"(not configured)","backend_type":"-","format":"mlx","group":"mlx","kind":"voice","model":"Kokoro-82M/mlx/6f708192a3b4","note":"a voice model: 'apogee voice say' speaks with it, never a chat backend","profile":"unprofiled","quant":"precision not stated","roles":"","section":"local","source":"converted","state":"mlx","type":"model","verified":"5 file(s) on record","window":"-"},{"architecture":"musicgen","backend":"(not configured)","backend_type":"-","format":"mlx","group":"mlx","kind":"music","model":"MusicGen-Small/mlx/708192a3b4c5","note":"kind 'music', not one this apogee knows: nothing here runs it","profile":"unprofiled","quant":"4-bit (affine, group 64)","roles":"","section":"local","source":"converted","state":"mlx","type":"model","verified":"5 file(s) on record","window":"32768-token window (the default)"},{"architecture":"ti2v","backend":"(not configured)","backend_type":"-","format":"mlx","group":"mlx","kind":"video","model":"Wan2.2-TI2V-5B/mlx/5e6f708192a3","note":"a video model: 'apogee video generate' runs it, never a chat backend","profile":"unprofiled","quant":"4-bit (affine, group 64)","roles":"","section":"local","source":"converted","state":"mlx","type":"model","verified":"4 file(s) on record","window":"-"},{"architecture":"gemma3_text","backend":"EmbeddingGemma-300M-bf16","backend_type":"mlx","format":"mlx","group":"mlx","kind":"embedding","model":"EmbeddingGemma-300M/mlx/3c4d5e6f7081","note":"there is no Python environment at /Users/someone/.apogee/training/venv -- the mlx backend runs mlx-lm there, never the system Python -- apogee train setup --with mlx","profile":"gemma3","quant":"bf16","roles":"","section":"local","source":"local","state":"no environment","type":"model","verified":"5 file(s) on record","window":"2048-token window (the default; trained for 2048)"},{"architecture":"gemma4","backend":"Gemma4-E4B-4bit","backend_type":"mlx","format":"mlx","group":"mlx","kind":"vision","model":"Gemma4-E4B/mlx/2b3c4d5e6f70","note":"there is no Python environment at /Users/someone/.apogee/training/venv -- the mlx backend runs mlx-lm there, never the system Python -- apogee train setup --with mlx","profile":"gemma3","quant":"4-bit (affine, group 64)","roles":"","section":"local","source":"local","state":"no environment","type":"model","verified":"6 file(s) on record","window":"32768-token window (the default; trained for 131072)"},{"architecture":"qwen3","backend":"Qwen3-8B-4bit","backend_type":"mlx","format":"mlx","group":"mlx","kind":"chat","model":"Qwen3-8B/mlx/1a2b3c4d5e6f","note":"there is no Python environment at /Users/someone/.apogee/training/venv -- the mlx backend runs mlx-lm there, never the system Python -- apogee train setup --with mlx","profile":"qwen3","quant":"4-bit (affine, group 64)","roles":"","section":"local","source":"local","state":"no environment","type":"model","verified":"5 file(s) on record","window":"32768-token window (the default; trained for 40960)"}],"folded":0,"object":"list"}
"""#
    /// tests/business/reads/Fixtures/train-pipeline-list.json
    static let pipelineList = #"""
{"data":[{"name":"nightly","path":null,"stages":[{"batch_size":0,"dataset":"starter","eval_suite":"hello","gate":["hello"],"grad_checkpoint":false,"iters":4,"mask_prompt":false,"method":"","name":"base","num_layers":0,"rehearsal_fraction":0.0}],"student":"tiny"},{"name":"skills","path":null,"stages":[{"batch_size":0,"dataset":"starter","eval_suite":"hello","gate":["hello"],"grad_checkpoint":false,"iters":4,"mask_prompt":false,"method":"","name":"greetings","num_layers":0,"rehearsal_fraction":0.0},{"batch_size":0,"dataset":"regress","eval_suite":"nope","gate":["hello","nope"],"grad_checkpoint":false,"iters":4,"mask_prompt":false,"method":"","name":"refusals","num_layers":0,"rehearsal_fraction":0.0}],"student":"tiny"}],"object":"list"}
"""#
    /// tests/business/reads/Fixtures/train-pipeline-show-skills.json
    static let pipelineShowSkills = #"""
{"name":"skills","object":"training.pipeline","path":null,"stages":[{"batch_size":0,"dataset":"starter","eval_suite":"hello","gate":["hello"],"grad_checkpoint":false,"iters":4,"mask_prompt":false,"method":"","name":"greetings","num_layers":0,"rehearsal_fraction":0.0},{"batch_size":0,"dataset":"regress","eval_suite":"nope","gate":["hello","nope"],"grad_checkpoint":false,"iters":4,"mask_prompt":false,"method":"","name":"refusals","num_layers":0,"rehearsal_fraction":0.0}],"student":"tiny"}
"""#
    /// tests/business/reads/Fixtures/train-pipeline-show-nightly.json
    static let pipelineShowNightly = #"""
{"name":"nightly","object":"training.pipeline","path":null,"stages":[{"batch_size":0,"dataset":"starter","eval_suite":"hello","gate":["hello"],"grad_checkpoint":false,"iters":4,"mask_prompt":false,"method":"","name":"base","num_layers":0,"rehearsal_fraction":0.0}],"student":"tiny"}
"""#
    /// tests/business/reads/Fixtures/train-status-cycle-held.json
    static let trainStatusCycleHeld = #"""
{"active_pipeline":"cycle-20261027-011507","cycle":{"anchor_score":1.0,"anchor_version":1,"backend":"nightly-model","consecutive_fails":0,"halt_reason":"","halted":false,"total_runs":1},"cycle_active":true,"latest_pipeline":{"id":"cycle-20261027-011507","kind":"pipeline","passed":0,"spec_name":"nightly","stages":1,"started_at":"2026-10-27T01:15:07Z","status":"running"},"pipelines":2,"recent_runs":[{"base_model":"/Users/someone/.apogee/models/tiny/safetensors/aaaaaaaaaaaa","dataset":"/Users/someone/.apogee/training/cycle/work/merged.jsonl","final_loss":0.0,"id":"cycle-20261027-011507-s0","iterations":0,"kind":"run","method":"lora","started_at":"2026-10-27T01:15:07Z","status":"running","trainer":"mock"},{"base_model":"/Users/someone/.apogee/models/tiny/safetensors/aaaaaaaaaaaa","dataset":"/Users/someone/.apogee/training/cycle/work/merged.jsonl","eval_passed":true,"eval_score":1.0,"final_loss":0.42,"finished_at":"2026-10-27T01:15:06Z","id":"cycle-20261027-011506-s0","iterations":4,"kind":"run","method":"lora","started_at":"2026-10-27T01:15:06Z","status":"complete","trainer":"mock"}],"running":["cycle-20261027-011507-s0"],"runs":2,"versions":[{"active_version":1,"backend":"nightly-model","kept":1,"total":1}]}
"""#
    /// tests/business/reads/Fixtures/train-cycle-status-held.json
    static let cycleStatusHeld = #"""
{"active":true,"anchor_score":1.0,"anchor_version":1,"backend":"nightly-model","consecutive_fails":0,"halt_reason":"","halted":false,"runs":[{"anchor_score":0.0,"cycle_score":1.0,"dataset_rows":2,"gate":"pass","gate_reason":"","pipeline_id":"cycle-20261027-011506","prev_score":0.0,"promoted_version":1,"run_at":"2026-10-27T01:15:06Z","source":"directory"}],"sessions_until":"","total_runs":1}
"""#
    /// tests/business/reads/Fixtures/train-status-cycle-held-halted.json
    static let trainStatusCycleHeldHalted = #"""
{"active_pipeline":"cycle-20261027-011507","cycle":{"anchor_score":1.0,"anchor_version":1,"backend":"nightly-model","consecutive_fails":0,"halt_reason":"halted by 'apogee train cycle halt'","halted":true,"total_runs":1},"cycle_active":true,"latest_pipeline":{"id":"cycle-20261027-011507","kind":"pipeline","passed":0,"spec_name":"nightly","stages":1,"started_at":"2026-10-27T01:15:07Z","status":"running"},"pipelines":2,"recent_runs":[{"base_model":"/Users/someone/.apogee/models/tiny/safetensors/aaaaaaaaaaaa","dataset":"/Users/someone/.apogee/training/cycle/work/merged.jsonl","final_loss":0.0,"id":"cycle-20261027-011507-s0","iterations":0,"kind":"run","method":"lora","started_at":"2026-10-27T01:15:07Z","status":"running","trainer":"mock"},{"base_model":"/Users/someone/.apogee/models/tiny/safetensors/aaaaaaaaaaaa","dataset":"/Users/someone/.apogee/training/cycle/work/merged.jsonl","eval_passed":true,"eval_score":1.0,"final_loss":0.42,"finished_at":"2026-10-27T01:15:06Z","id":"cycle-20261027-011506-s0","iterations":4,"kind":"run","method":"lora","started_at":"2026-10-27T01:15:06Z","status":"complete","trainer":"mock"}],"running":["cycle-20261027-011507-s0"],"runs":2,"versions":[{"active_version":1,"backend":"nightly-model","kept":1,"total":1}]}
"""#
    /// tests/business/reads/Fixtures/train-cycle-status-held-halted.json
    static let cycleStatusHeldHalted = #"""
{"active":true,"anchor_score":1.0,"anchor_version":1,"backend":"nightly-model","consecutive_fails":0,"halt_reason":"halted by 'apogee train cycle halt'","halted":true,"runs":[{"anchor_score":0.0,"cycle_score":1.0,"dataset_rows":2,"gate":"pass","gate_reason":"","pipeline_id":"cycle-20261027-011506","prev_score":0.0,"promoted_version":1,"run_at":"2026-10-27T01:15:06Z","source":"directory"}],"sessions_until":"","total_runs":1}
"""#
    /// tests/business/reads/Fixtures/train-status-cycle-halt-kept.json
    static let trainStatusCycleHaltKept = #"""
{"active_pipeline":null,"cycle":{"anchor_score":1.0,"anchor_version":1,"backend":"nightly-model","consecutive_fails":0,"halt_reason":"halted by 'apogee train cycle halt'","halted":true,"total_runs":2},"cycle_active":false,"latest_pipeline":{"completed_at":"2026-10-27T01:15:08Z","id":"cycle-20261027-011507","kind":"pipeline","passed":1,"spec_name":"nightly","stages":1,"started_at":"2026-10-27T01:15:07Z","status":"complete"},"pipelines":2,"recent_runs":[{"base_model":"/Users/someone/.apogee/models/tiny/safetensors/aaaaaaaaaaaa","dataset":"/Users/someone/.apogee/training/cycle/work/merged.jsonl","eval_passed":true,"eval_score":1.0,"final_loss":0.42,"finished_at":"2026-10-27T01:15:08Z","id":"cycle-20261027-011507-s0","iterations":200,"kind":"run","method":"lora","started_at":"2026-10-27T01:15:07Z","status":"complete","trainer":"mock"},{"base_model":"/Users/someone/.apogee/models/tiny/safetensors/aaaaaaaaaaaa","dataset":"/Users/someone/.apogee/training/cycle/work/merged.jsonl","eval_passed":true,"eval_score":1.0,"final_loss":0.42,"finished_at":"2026-10-27T01:15:06Z","id":"cycle-20261027-011506-s0","iterations":4,"kind":"run","method":"lora","started_at":"2026-10-27T01:15:06Z","status":"complete","trainer":"mock"}],"running":[],"runs":2,"versions":[{"active_version":2,"backend":"nightly-model","kept":2,"total":2}]}
"""#
    /// tests/business/reads/Fixtures/train-cycle-status-halt-kept.json
    static let cycleStatusHaltKept = #"""
{"active":false,"anchor_score":1.0,"anchor_version":1,"backend":"nightly-model","consecutive_fails":0,"halt_reason":"halted by 'apogee train cycle halt'","halted":true,"runs":[{"anchor_score":0.0,"cycle_score":1.0,"dataset_rows":2,"gate":"pass","gate_reason":"","pipeline_id":"cycle-20261027-011506","prev_score":0.0,"promoted_version":1,"run_at":"2026-10-27T01:15:06Z","source":"directory"},{"anchor_score":1.0,"cycle_score":1.0,"dataset_rows":2,"gate":"pass","gate_reason":"","pipeline_id":"cycle-20261027-011507","prev_score":1.0,"promoted_version":2,"run_at":"2026-10-27T01:15:07Z","source":"directory"}],"sessions_until":"","total_runs":2}
"""#
    /// tests/business/reads/Fixtures/train-cycle-halt-during-pass.txt
    static let cycleHaltDuringPass = #"""
cycle halted -- 'apogee train cycle run' refuses until 'apogee train cycle resume'
  the pass running now finishes, and keeps the halt
"""#
    /// tests/business/jobs/Fixtures/train-cycle-run-halted.jsonl
    static let cycleRunHalted = #"""
{"protocol_version":1,"schema":"2026-10-26","type":"job","verb":"train cycle run"}
{"state":"collecting","text":"[cycle] directory: 1 file(s) in /Users/someone/queue","type":"pass"}
{"state":"merging","text":"[cycle] 2 example(s) from directory","type":"pass"}
{"state":"training","text":"[cycle] pipeline cycle-20261027-011507 (nightly): 1 stage(s)","type":"pass"}
{"count":1,"index":1,"label":"base","pipeline_run_id":"cycle-20261027-011507","state":"pending","type":"stage"}
{"count":1,"index":1,"label":"base","pipeline_run_id":"cycle-20261027-011507","run_id":"cycle-20261027-011507-s0","state":"training","type":"stage"}
{"text":"[train] stage 1/1 'base': mock lora on aaaaaaaaaaaa, dataset merged.jsonl","type":"notice"}
{"text":"[train] mock trainer: 200 iteration(s), method lora","type":"notice"}
{"iteration":1,"iterations":200,"loss":20.32,"lr":0.0001,"stage":1,"throughput":10.0,"type":"iteration"}
{"iteration":2,"iterations":200,"loss":20.220000000000002,"lr":0.0001,"stage":1,"throughput":10.0,"type":"iteration"}
{"iteration":3,"iterations":200,"loss":20.12,"lr":0.0001,"stage":1,"throughput":10.0,"type":"iteration"}
{"iteration":4,"iterations":200,"loss":20.02,"lr":0.0001,"stage":1,"throughput":10.0,"type":"iteration"}
{"iteration":5,"iterations":200,"loss":19.92,"lr":0.0001,"stage":1,"throughput":10.0,"type":"iteration"}
{"iteration":6,"iterations":200,"loss":19.82,"lr":0.0001,"stage":1,"throughput":10.0,"type":"iteration"}
{"iteration":7,"iterations":200,"loss":19.720000000000002,"lr":0.0001,"stage":1,"throughput":10.0,"type":"iteration"}
{"iteration":8,"iterations":200,"loss":19.62,"lr":0.0001,"stage":1,"throughput":10.0,"type":"iteration"}
{"iteration":9,"iterations":200,"loss":19.52,"lr":0.0001,"stage":1,"throughput":10.0,"type":"iteration"}
{"iteration":10,"iterations":200,"loss":19.42,"lr":0.0001,"stage":1,"throughput":10.0,"type":"iteration"}
{"iteration":11,"iterations":200,"loss":19.32,"lr":0.0001,"stage":1,"throughput":10.0,"type":"iteration"}
{"iteration":12,"iterations":200,"loss":19.220000000000002,"lr":0.0001,"stage":1,"throughput":10.0,"type":"iteration"}
{"iteration":13,"iterations":200,"loss":19.12,"lr":0.0001,"stage":1,"throughput":10.0,"type":"iteration"}
{"iteration":14,"iterations":200,"loss":19.02,"lr":0.0001,"stage":1,"throughput":10.0,"type":"iteration"}
{"iteration":15,"iterations":200,"loss":18.92,"lr":0.0001,"stage":1,"throughput":10.0,"type":"iteration"}
{"iteration":16,"iterations":200,"loss":18.82,"lr":0.0001,"stage":1,"throughput":10.0,"type":"iteration"}
{"iteration":17,"iterations":200,"loss":18.720000000000002,"lr":0.0001,"stage":1,"throughput":10.0,"type":"iteration"}
{"iteration":18,"iterations":200,"loss":18.62,"lr":0.0001,"stage":1,"throughput":10.0,"type":"iteration"}
{"iteration":19,"iterations":200,"loss":18.52,"lr":0.0001,"stage":1,"throughput":10.0,"type":"iteration"}
{"iteration":20,"iterations":200,"loss":18.42,"lr":0.0001,"stage":1,"throughput":10.0,"type":"iteration"}
{"iteration":21,"iterations":200,"loss":18.32,"lr":0.0001,"stage":1,"throughput":10.0,"type":"iteration"}
{"iteration":22,"iterations":200,"loss":18.220000000000002,"lr":0.0001,"stage":1,"throughput":10.0,"type":"iteration"}
{"iteration":23,"iterations":200,"loss":18.12,"lr":0.0001,"stage":1,"throughput":10.0,"type":"iteration"}
{"iteration":24,"iterations":200,"loss":18.02,"lr":0.0001,"stage":1,"throughput":10.0,"type":"iteration"}
{"iteration":25,"iterations":200,"loss":17.92,"lr":0.0001,"stage":1,"throughput":10.0,"type":"iteration"}
{"iteration":26,"iterations":200,"loss":17.82,"lr":0.0001,"stage":1,"throughput":10.0,"type":"iteration"}
{"iteration":27,"iterations":200,"loss":17.720000000000002,"lr":0.0001,"stage":1,"throughput":10.0,"type":"iteration"}
{"iteration":28,"iterations":200,"loss":17.62,"lr":0.0001,"stage":1,"throughput":10.0,"type":"iteration"}
{"iteration":29,"iterations":200,"loss":17.52,"lr":0.0001,"stage":1,"throughput":10.0,"type":"iteration"}
{"iteration":30,"iterations":200,"loss":17.42,"lr":0.0001,"stage":1,"throughput":10.0,"type":"iteration"}
{"iteration":31,"iterations":200,"loss":17.32,"lr":0.0001,"stage":1,"throughput":10.0,"type":"iteration"}
{"iteration":32,"iterations":200,"loss":17.220000000000002,"lr":0.0001,"stage":1,"throughput":10.0,"type":"iteration"}
{"iteration":33,"iterations":200,"loss":17.12,"lr":0.0001,"stage":1,"throughput":10.0,"type":"iteration"}
{"iteration":34,"iterations":200,"loss":17.02,"lr":0.0001,"stage":1,"throughput":10.0,"type":"iteration"}
{"iteration":35,"iterations":200,"loss":16.92,"lr":0.0001,"stage":1,"throughput":10.0,"type":"iteration"}
{"iteration":36,"iterations":200,"loss":16.82,"lr":0.0001,"stage":1,"throughput":10.0,"type":"iteration"}
{"iteration":37,"iterations":200,"loss":16.720000000000002,"lr":0.0001,"stage":1,"throughput":10.0,"type":"iteration"}
{"iteration":38,"iterations":200,"loss":16.62,"lr":0.0001,"stage":1,"throughput":10.0,"type":"iteration"}
{"iteration":39,"iterations":200,"loss":16.52,"lr":0.0001,"stage":1,"throughput":10.0,"type":"iteration"}
{"iteration":40,"iterations":200,"loss":16.42,"lr":0.0001,"stage":1,"throughput":10.0,"type":"iteration"}
{"iteration":41,"iterations":200,"loss":16.32,"lr":0.0001,"stage":1,"throughput":10.0,"type":"iteration"}
{"iteration":42,"iterations":200,"loss":16.220000000000002,"lr":0.0001,"stage":1,"throughput":10.0,"type":"iteration"}
{"iteration":43,"iterations":200,"loss":16.12,"lr":0.0001,"stage":1,"throughput":10.0,"type":"iteration"}
{"iteration":44,"iterations":200,"loss":16.02,"lr":0.0001,"stage":1,"throughput":10.0,"type":"iteration"}
{"iteration":45,"iterations":200,"loss":15.920000000000002,"lr":0.0001,"stage":1,"throughput":10.0,"type":"iteration"}
{"iteration":46,"iterations":200,"loss":15.82,"lr":0.0001,"stage":1,"throughput":10.0,"type":"iteration"}
{"iteration":47,"iterations":200,"loss":15.72,"lr":0.0001,"stage":1,"throughput":10.0,"type":"iteration"}
{"iteration":48,"iterations":200,"loss":15.620000000000001,"lr":0.0001,"stage":1,"throughput":10.0,"type":"iteration"}
{"iteration":49,"iterations":200,"loss":15.520000000000001,"lr":0.0001,"stage":1,"throughput":10.0,"type":"iteration"}
{"iteration":50,"iterations":200,"loss":15.420000000000002,"lr":0.0001,"stage":1,"throughput":10.0,"type":"iteration"}
{"iteration":51,"iterations":200,"loss":15.32,"lr":0.0001,"stage":1,"throughput":10.0,"type":"iteration"}
{"iteration":52,"iterations":200,"loss":15.22,"lr":0.0001,"stage":1,"throughput":10.0,"type":"iteration"}
{"iteration":53,"iterations":200,"loss":15.120000000000001,"lr":0.0001,"stage":1,"throughput":10.0,"type":"iteration"}
{"iteration":54,"iterations":200,"loss":15.020000000000001,"lr":0.0001,"stage":1,"throughput":10.0,"type":"iteration"}
{"iteration":55,"iterations":200,"loss":14.92,"lr":0.0001,"stage":1,"throughput":10.0,"type":"iteration"}
{"iteration":56,"iterations":200,"loss":14.82,"lr":0.0001,"stage":1,"throughput":10.0,"type":"iteration"}
{"iteration":57,"iterations":200,"loss":14.72,"lr":0.0001,"stage":1,"throughput":10.0,"type":"iteration"}
{"iteration":58,"iterations":200,"loss":14.620000000000001,"lr":0.0001,"stage":1,"throughput":10.0,"type":"iteration"}
{"iteration":59,"iterations":200,"loss":14.520000000000001,"lr":0.0001,"stage":1,"throughput":10.0,"type":"iteration"}
{"iteration":60,"iterations":200,"loss":14.42,"lr":0.0001,"stage":1,"throughput":10.0,"type":"iteration"}
{"iteration":61,"iterations":200,"loss":14.32,"lr":0.0001,"stage":1,"throughput":10.0,"type":"iteration"}
{"iteration":62,"iterations":200,"loss":14.22,"lr":0.0001,"stage":1,"throughput":10.0,"type":"iteration"}
{"iteration":63,"iterations":200,"loss":14.120000000000001,"lr":0.0001,"stage":1,"throughput":10.0,"type":"iteration"}
{"iteration":64,"iterations":200,"loss":14.020000000000001,"lr":0.0001,"stage":1,"throughput":10.0,"type":"iteration"}
{"iteration":65,"iterations":200,"loss":13.92,"lr":0.0001,"stage":1,"throughput":10.0,"type":"iteration"}
{"iteration":66,"iterations":200,"loss":13.82,"lr":0.0001,"stage":1,"throughput":10.0,"type":"iteration"}
{"iteration":67,"iterations":200,"loss":13.72,"lr":0.0001,"stage":1,"throughput":10.0,"type":"iteration"}
{"iteration":68,"iterations":200,"loss":13.620000000000001,"lr":0.0001,"stage":1,"throughput":10.0,"type":"iteration"}
{"iteration":69,"iterations":200,"loss":13.520000000000001,"lr":0.0001,"stage":1,"throughput":10.0,"type":"iteration"}
{"iteration":70,"iterations":200,"loss":13.42,"lr":0.0001,"stage":1,"throughput":10.0,"type":"iteration"}
{"iteration":71,"iterations":200,"loss":13.32,"lr":0.0001,"stage":1,"throughput":10.0,"type":"iteration"}
{"iteration":72,"iterations":200,"loss":13.22,"lr":0.0001,"stage":1,"throughput":10.0,"type":"iteration"}
{"iteration":73,"iterations":200,"loss":13.120000000000001,"lr":0.0001,"stage":1,"throughput":10.0,"type":"iteration"}
{"iteration":74,"iterations":200,"loss":13.020000000000001,"lr":0.0001,"stage":1,"throughput":10.0,"type":"iteration"}
{"iteration":75,"iterations":200,"loss":12.92,"lr":0.0001,"stage":1,"throughput":10.0,"type":"iteration"}
{"iteration":76,"iterations":200,"loss":12.82,"lr":0.0001,"stage":1,"throughput":10.0,"type":"iteration"}
{"iteration":77,"iterations":200,"loss":12.72,"lr":0.0001,"stage":1,"throughput":10.0,"type":"iteration"}
{"iteration":78,"iterations":200,"loss":12.620000000000001,"lr":0.0001,"stage":1,"throughput":10.0,"type":"iteration"}
{"iteration":79,"iterations":200,"loss":12.520000000000001,"lr":0.0001,"stage":1,"throughput":10.0,"type":"iteration"}
{"iteration":80,"iterations":200,"loss":12.42,"lr":0.0001,"stage":1,"throughput":10.0,"type":"iteration"}
{"iteration":81,"iterations":200,"loss":12.32,"lr":0.0001,"stage":1,"throughput":10.0,"type":"iteration"}
{"iteration":82,"iterations":200,"loss":12.22,"lr":0.0001,"stage":1,"throughput":10.0,"type":"iteration"}
{"iteration":83,"iterations":200,"loss":12.120000000000001,"lr":0.0001,"stage":1,"throughput":10.0,"type":"iteration"}
{"iteration":84,"iterations":200,"loss":12.020000000000001,"lr":0.0001,"stage":1,"throughput":10.0,"type":"iteration"}
{"iteration":85,"iterations":200,"loss":11.92,"lr":0.0001,"stage":1,"throughput":10.0,"type":"iteration"}
{"iteration":86,"iterations":200,"loss":11.82,"lr":0.0001,"stage":1,"throughput":10.0,"type":"iteration"}
{"iteration":87,"iterations":200,"loss":11.72,"lr":0.0001,"stage":1,"throughput":10.0,"type":"iteration"}
{"iteration":88,"iterations":200,"loss":11.620000000000001,"lr":0.0001,"stage":1,"throughput":10.0,"type":"iteration"}
{"iteration":89,"iterations":200,"loss":11.520000000000001,"lr":0.0001,"stage":1,"throughput":10.0,"type":"iteration"}
{"iteration":90,"iterations":200,"loss":11.42,"lr":0.0001,"stage":1,"throughput":10.0,"type":"iteration"}
{"iteration":91,"iterations":200,"loss":11.32,"lr":0.0001,"stage":1,"throughput":10.0,"type":"iteration"}
{"iteration":92,"iterations":200,"loss":11.22,"lr":0.0001,"stage":1,"throughput":10.0,"type":"iteration"}
{"iteration":93,"iterations":200,"loss":11.120000000000001,"lr":0.0001,"stage":1,"throughput":10.0,"type":"iteration"}
{"iteration":94,"iterations":200,"loss":11.020000000000001,"lr":0.0001,"stage":1,"throughput":10.0,"type":"iteration"}
{"iteration":95,"iterations":200,"loss":10.92,"lr":0.0001,"stage":1,"throughput":10.0,"type":"iteration"}
{"iteration":96,"iterations":200,"loss":10.82,"lr":0.0001,"stage":1,"throughput":10.0,"type":"iteration"}
{"iteration":97,"iterations":200,"loss":10.72,"lr":0.0001,"stage":1,"throughput":10.0,"type":"iteration"}
{"iteration":98,"iterations":200,"loss":10.620000000000001,"lr":0.0001,"stage":1,"throughput":10.0,"type":"iteration"}
{"iteration":99,"iterations":200,"loss":10.520000000000001,"lr":0.0001,"stage":1,"throughput":10.0,"type":"iteration"}
{"iteration":100,"iterations":200,"loss":10.42,"lr":0.0001,"stage":1,"throughput":10.0,"type":"iteration"}
{"iteration":101,"iterations":200,"loss":10.32,"lr":0.0001,"stage":1,"throughput":10.0,"type":"iteration"}
{"iteration":102,"iterations":200,"loss":10.22,"lr":0.0001,"stage":1,"throughput":10.0,"type":"iteration"}
{"iteration":103,"iterations":200,"loss":10.120000000000001,"lr":0.0001,"stage":1,"throughput":10.0,"type":"iteration"}
{"iteration":104,"iterations":200,"loss":10.020000000000001,"lr":0.0001,"stage":1,"throughput":10.0,"type":"iteration"}
{"iteration":105,"iterations":200,"loss":9.92,"lr":0.0001,"stage":1,"throughput":10.0,"type":"iteration"}
{"iteration":106,"iterations":200,"loss":9.82,"lr":0.0001,"stage":1,"throughput":10.0,"type":"iteration"}
{"iteration":107,"iterations":200,"loss":9.72,"lr":0.0001,"stage":1,"throughput":10.0,"type":"iteration"}
{"iteration":108,"iterations":200,"loss":9.620000000000001,"lr":0.0001,"stage":1,"throughput":10.0,"type":"iteration"}
{"iteration":109,"iterations":200,"loss":9.520000000000001,"lr":0.0001,"stage":1,"throughput":10.0,"type":"iteration"}
{"iteration":110,"iterations":200,"loss":9.42,"lr":0.0001,"stage":1,"throughput":10.0,"type":"iteration"}
{"iteration":111,"iterations":200,"loss":9.32,"lr":0.0001,"stage":1,"throughput":10.0,"type":"iteration"}
{"iteration":112,"iterations":200,"loss":9.22,"lr":0.0001,"stage":1,"throughput":10.0,"type":"iteration"}
{"iteration":113,"iterations":200,"loss":9.120000000000001,"lr":0.0001,"stage":1,"throughput":10.0,"type":"iteration"}
{"iteration":114,"iterations":200,"loss":9.02,"lr":0.0001,"stage":1,"throughput":10.0,"type":"iteration"}
{"iteration":115,"iterations":200,"loss":8.92,"lr":0.0001,"stage":1,"throughput":10.0,"type":"iteration"}
{"iteration":116,"iterations":200,"loss":8.82,"lr":0.0001,"stage":1,"throughput":10.0,"type":"iteration"}
{"iteration":117,"iterations":200,"loss":8.72,"lr":0.0001,"stage":1,"throughput":10.0,"type":"iteration"}
{"iteration":118,"iterations":200,"loss":8.620000000000001,"lr":0.0001,"stage":1,"throughput":10.0,"type":"iteration"}
{"iteration":119,"iterations":200,"loss":8.52,"lr":0.0001,"stage":1,"throughput":10.0,"type":"iteration"}
{"iteration":120,"iterations":200,"loss":8.42,"lr":0.0001,"stage":1,"throughput":10.0,"type":"iteration"}
{"iteration":121,"iterations":200,"loss":8.32,"lr":0.0001,"stage":1,"throughput":10.0,"type":"iteration"}
{"iteration":122,"iterations":200,"loss":8.22,"lr":0.0001,"stage":1,"throughput":10.0,"type":"iteration"}
{"iteration":123,"iterations":200,"loss":8.120000000000001,"lr":0.0001,"stage":1,"throughput":10.0,"type":"iteration"}
{"iteration":124,"iterations":200,"loss":8.02,"lr":0.0001,"stage":1,"throughput":10.0,"type":"iteration"}
{"iteration":125,"iterations":200,"loss":7.920000000000001,"lr":0.0001,"stage":1,"throughput":10.0,"type":"iteration"}
{"iteration":126,"iterations":200,"loss":7.82,"lr":0.0001,"stage":1,"throughput":10.0,"type":"iteration"}
{"iteration":127,"iterations":200,"loss":7.720000000000001,"lr":0.0001,"stage":1,"throughput":10.0,"type":"iteration"}
{"iteration":128,"iterations":200,"loss":7.62,"lr":0.0001,"stage":1,"throughput":10.0,"type":"iteration"}
{"iteration":129,"iterations":200,"loss":7.5200000000000005,"lr":0.0001,"stage":1,"throughput":10.0,"type":"iteration"}
{"iteration":130,"iterations":200,"loss":7.42,"lr":0.0001,"stage":1,"throughput":10.0,"type":"iteration"}
{"iteration":131,"iterations":200,"loss":7.32,"lr":0.0001,"stage":1,"throughput":10.0,"type":"iteration"}
{"iteration":132,"iterations":200,"loss":7.220000000000001,"lr":0.0001,"stage":1,"throughput":10.0,"type":"iteration"}
{"iteration":133,"iterations":200,"loss":7.12,"lr":0.0001,"stage":1,"throughput":10.0,"type":"iteration"}
{"iteration":134,"iterations":200,"loss":7.0200000000000005,"lr":0.0001,"stage":1,"throughput":10.0,"type":"iteration"}
{"iteration":135,"iterations":200,"loss":6.92,"lr":0.0001,"stage":1,"throughput":10.0,"type":"iteration"}
{"iteration":136,"iterations":200,"loss":6.82,"lr":0.0001,"stage":1,"throughput":10.0,"type":"iteration"}
{"iteration":137,"iterations":200,"loss":6.720000000000001,"lr":0.0001,"stage":1,"throughput":10.0,"type":"iteration"}
{"iteration":138,"iterations":200,"loss":6.62,"lr":0.0001,"stage":1,"throughput":10.0,"type":"iteration"}
{"iteration":139,"iterations":200,"loss":6.5200000000000005,"lr":0.0001,"stage":1,"throughput":10.0,"type":"iteration"}
{"iteration":140,"iterations":200,"loss":6.42,"lr":0.0001,"stage":1,"throughput":10.0,"type":"iteration"}
{"iteration":141,"iterations":200,"loss":6.32,"lr":0.0001,"stage":1,"throughput":10.0,"type":"iteration"}
{"iteration":142,"iterations":200,"loss":6.220000000000001,"lr":0.0001,"stage":1,"throughput":10.0,"type":"iteration"}
{"iteration":143,"iterations":200,"loss":6.12,"lr":0.0001,"stage":1,"throughput":10.0,"type":"iteration"}
{"iteration":144,"iterations":200,"loss":6.0200000000000005,"lr":0.0001,"stage":1,"throughput":10.0,"type":"iteration"}
{"iteration":145,"iterations":200,"loss":5.92,"lr":0.0001,"stage":1,"throughput":10.0,"type":"iteration"}
{"iteration":146,"iterations":200,"loss":5.82,"lr":0.0001,"stage":1,"throughput":10.0,"type":"iteration"}
{"iteration":147,"iterations":200,"loss":5.720000000000001,"lr":0.0001,"stage":1,"throughput":10.0,"type":"iteration"}
{"iteration":148,"iterations":200,"loss":5.62,"lr":0.0001,"stage":1,"throughput":10.0,"type":"iteration"}
{"iteration":149,"iterations":200,"loss":5.5200000000000005,"lr":0.0001,"stage":1,"throughput":10.0,"type":"iteration"}
{"iteration":150,"iterations":200,"loss":5.42,"lr":0.0001,"stage":1,"throughput":10.0,"type":"iteration"}
{"iteration":151,"iterations":200,"loss":5.32,"lr":0.0001,"stage":1,"throughput":10.0,"type":"iteration"}
{"iteration":152,"iterations":200,"loss":5.220000000000001,"lr":0.0001,"stage":1,"throughput":10.0,"type":"iteration"}
{"iteration":153,"iterations":200,"loss":5.12,"lr":0.0001,"stage":1,"throughput":10.0,"type":"iteration"}
{"iteration":154,"iterations":200,"loss":5.0200000000000005,"lr":0.0001,"stage":1,"throughput":10.0,"type":"iteration"}
{"iteration":155,"iterations":200,"loss":4.92,"lr":0.0001,"stage":1,"throughput":10.0,"type":"iteration"}
{"iteration":156,"iterations":200,"loss":4.82,"lr":0.0001,"stage":1,"throughput":10.0,"type":"iteration"}
{"iteration":157,"iterations":200,"loss":4.720000000000001,"lr":0.0001,"stage":1,"throughput":10.0,"type":"iteration"}
{"iteration":158,"iterations":200,"loss":4.62,"lr":0.0001,"stage":1,"throughput":10.0,"type":"iteration"}
{"iteration":159,"iterations":200,"loss":4.5200000000000005,"lr":0.0001,"stage":1,"throughput":10.0,"type":"iteration"}
{"iteration":160,"iterations":200,"loss":4.42,"lr":0.0001,"stage":1,"throughput":10.0,"type":"iteration"}
{"iteration":161,"iterations":200,"loss":4.32,"lr":0.0001,"stage":1,"throughput":10.0,"type":"iteration"}
{"iteration":162,"iterations":200,"loss":4.220000000000001,"lr":0.0001,"stage":1,"throughput":10.0,"type":"iteration"}
{"iteration":163,"iterations":200,"loss":4.12,"lr":0.0001,"stage":1,"throughput":10.0,"type":"iteration"}
{"iteration":164,"iterations":200,"loss":4.0200000000000005,"lr":0.0001,"stage":1,"throughput":10.0,"type":"iteration"}
{"iteration":165,"iterations":200,"loss":3.9200000000000004,"lr":0.0001,"stage":1,"throughput":10.0,"type":"iteration"}
{"iteration":166,"iterations":200,"loss":3.8200000000000003,"lr":0.0001,"stage":1,"throughput":10.0,"type":"iteration"}
{"iteration":167,"iterations":200,"loss":3.72,"lr":0.0001,"stage":1,"throughput":10.0,"type":"iteration"}
{"iteration":168,"iterations":200,"loss":3.62,"lr":0.0001,"stage":1,"throughput":10.0,"type":"iteration"}
{"iteration":169,"iterations":200,"loss":3.52,"lr":0.0001,"stage":1,"throughput":10.0,"type":"iteration"}
{"iteration":170,"iterations":200,"loss":3.42,"lr":0.0001,"stage":1,"throughput":10.0,"type":"iteration"}
{"iteration":171,"iterations":200,"loss":3.3200000000000003,"lr":0.0001,"stage":1,"throughput":10.0,"type":"iteration"}
{"iteration":172,"iterations":200,"loss":3.22,"lr":0.0001,"stage":1,"throughput":10.0,"type":"iteration"}
{"iteration":173,"iterations":200,"loss":3.12,"lr":0.0001,"stage":1,"throughput":10.0,"type":"iteration"}
{"iteration":174,"iterations":200,"loss":3.02,"lr":0.0001,"stage":1,"throughput":10.0,"type":"iteration"}
{"iteration":175,"iterations":200,"loss":2.92,"lr":0.0001,"stage":1,"throughput":10.0,"type":"iteration"}
{"iteration":176,"iterations":200,"loss":2.8200000000000003,"lr":0.0001,"stage":1,"throughput":10.0,"type":"iteration"}
{"iteration":177,"iterations":200,"loss":2.72,"lr":0.0001,"stage":1,"throughput":10.0,"type":"iteration"}
{"iteration":178,"iterations":200,"loss":2.62,"lr":0.0001,"stage":1,"throughput":10.0,"type":"iteration"}
{"iteration":179,"iterations":200,"loss":2.52,"lr":0.0001,"stage":1,"throughput":10.0,"type":"iteration"}
{"iteration":180,"iterations":200,"loss":2.42,"lr":0.0001,"stage":1,"throughput":10.0,"type":"iteration"}
{"iteration":181,"iterations":200,"loss":2.3200000000000003,"lr":0.0001,"stage":1,"throughput":10.0,"type":"iteration"}
{"iteration":182,"iterations":200,"loss":2.22,"lr":0.0001,"stage":1,"throughput":10.0,"type":"iteration"}
{"iteration":183,"iterations":200,"loss":2.12,"lr":0.0001,"stage":1,"throughput":10.0,"type":"iteration"}
{"iteration":184,"iterations":200,"loss":2.02,"lr":0.0001,"stage":1,"throughput":10.0,"type":"iteration"}
{"iteration":185,"iterations":200,"loss":1.9200000000000002,"lr":0.0001,"stage":1,"throughput":10.0,"type":"iteration"}
{"iteration":186,"iterations":200,"loss":1.82,"lr":0.0001,"stage":1,"throughput":10.0,"type":"iteration"}
{"iteration":187,"iterations":200,"loss":1.72,"lr":0.0001,"stage":1,"throughput":10.0,"type":"iteration"}
{"iteration":188,"iterations":200,"loss":1.62,"lr":0.0001,"stage":1,"throughput":10.0,"type":"iteration"}
{"iteration":189,"iterations":200,"loss":1.52,"lr":0.0001,"stage":1,"throughput":10.0,"type":"iteration"}
{"iteration":190,"iterations":200,"loss":1.42,"lr":0.0001,"stage":1,"throughput":10.0,"type":"iteration"}
{"iteration":191,"iterations":200,"loss":1.32,"lr":0.0001,"stage":1,"throughput":10.0,"type":"iteration"}
{"iteration":192,"iterations":200,"loss":1.22,"lr":0.0001,"stage":1,"throughput":10.0,"type":"iteration"}
{"iteration":193,"iterations":200,"loss":1.12,"lr":0.0001,"stage":1,"throughput":10.0,"type":"iteration"}
{"iteration":194,"iterations":200,"loss":1.02,"lr":0.0001,"stage":1,"throughput":10.0,"type":"iteration"}
{"iteration":195,"iterations":200,"loss":0.92,"lr":0.0001,"stage":1,"throughput":10.0,"type":"iteration"}
{"iteration":196,"iterations":200,"loss":0.8200000000000001,"lr":0.0001,"stage":1,"throughput":10.0,"type":"iteration"}
{"iteration":197,"iterations":200,"loss":0.72,"lr":0.0001,"stage":1,"throughput":10.0,"type":"iteration"}
{"iteration":198,"iterations":200,"loss":0.62,"lr":0.0001,"stage":1,"throughput":10.0,"type":"iteration"}
{"iteration":199,"iterations":200,"loss":0.52,"lr":0.0001,"stage":1,"throughput":10.0,"type":"iteration"}
{"iteration":200,"iterations":200,"loss":0.42,"lr":0.0001,"stage":1,"throughput":10.0,"type":"iteration"}
{"text":"[train] adapter saved to /Users/someone/.apogee/training/runs/cycle-20261027-011507-s0/adapters","type":"notice"}
{"count":1,"index":1,"label":"base","pipeline_run_id":"cycle-20261027-011507","run_id":"cycle-20261027-011507-s0","state":"evaluating","type":"stage"}
{"text":"[eval] cumulative suite: 1 suite(s), 1 item(s); no judge: unmatched items skip","type":"notice"}
{"text":"[gate] stage 'base' PASSED the cumulative eval -- 100% (1/1)","type":"notice"}
{"count":1,"cumulative_score":1.0,"index":1,"label":"base","pipeline_run_id":"cycle-20261027-011507","run_id":"cycle-20261027-011507-s0","state":"passed","type":"stage"}
{"state":"gating","text":"[cycle] gate -- current 100%, previous 100%, anchor 100% (v1), threshold 0%","type":"pass"}
{"state":"promoting","text":"[cycle] gate PASSED (100%) -- promoting cycle-20261027-011507-s0 into nightly-model","type":"pass"}
{"text":"[promote] run cycle-20261027-011507-s0 -> nightly-model v2","type":"notice"}
{"text":"[promote] fusing the adapter into the base model","type":"notice"}
{"text":"[promote]   base:    /Users/someone/.apogee/models/tiny/safetensors/aaaaaaaaaaaa","type":"notice"}
{"text":"[promote]   adapter: /Users/someone/.apogee/training/runs/cycle-20261027-011507-s0/adapters","type":"notice"}
{"text":"[promote]   output:  /Users/someone/.apogee/training/runs/cycle-20261027-011507-s0/fused","type":"notice"}
{"text":"[promote] mock fuse: /Users/someone/.apogee/training/runs/cycle-20261027-011507-s0/adapters into /Users/someone/.apogee/models/tiny/safetensors/aaaaaaaaaaaa","type":"notice"}
{"text":"[promote] converting the fused model to GGUF (F16): /Users/someone/.apogee/models/tiny/gguf/.incoming-2e532aee93f4/nightly-model-v2.gguf","type":"notice"}
{"text":"[promote] mock converter: writing a minimal GGUF","type":"notice"}
{"state":"passed","text":"[cycle] complete -- nightly-model promoted to v2 (/Users/someone/.apogee/models/tiny/gguf/fa8cf82be2bf/nightly-model-v2.gguf)","type":"pass"}
{"text":"[cycle] halted while this pass ran (halted by 'apogee train cycle halt') -- the halt is kept: 'apogee train cycle run' refuses until 'apogee train cycle resume'","type":"notice"}
{"document":{"active":false,"anchor_score":1.0,"anchor_version":1,"backend":"nightly-model","consecutive_fails":0,"halt_reason":"halted by 'apogee train cycle halt'","halted":true,"runs":[{"anchor_score":0.0,"cycle_score":1.0,"dataset_rows":2,"gate":"pass","gate_reason":"","pipeline_id":"cycle-20261027-011506","prev_score":0.0,"promoted_version":1,"run_at":"2026-10-27T01:15:06Z","source":"directory"},{"anchor_score":1.0,"cycle_score":1.0,"dataset_rows":2,"gate":"pass","gate_reason":"","pipeline_id":"cycle-20261027-011507","prev_score":1.0,"promoted_version":2,"run_at":"2026-10-27T01:15:07Z","source":"directory"}],"sessions_until":"","total_runs":2},"type":"document"}
"""#
    /// tests/business/jobs/Fixtures/datasets-pull-handmade.jsonl
    static let pullStream = #"""
{"protocol_version":1,"schema":"2026-10-26","type":"job","verb":"datasets pull"}
{"text":"downloading 2 file(s), 136 KiB, into /Users/someone/.apogee/training/datasets/raw/someone--heat-notes","type":"notice"}
{"count":2,"done":16384,"file":"data/train.jsonl","index":1,"total":100000,"type":"bytes"}
{"count":2,"done":32768,"file":"data/train.jsonl","index":1,"total":100000,"type":"bytes"}
{"count":2,"done":49152,"file":"data/train.jsonl","index":1,"total":100000,"type":"bytes"}
{"count":2,"done":65536,"file":"data/train.jsonl","index":1,"total":100000,"type":"bytes"}
{"count":2,"done":81920,"file":"data/train.jsonl","index":1,"total":100000,"type":"bytes"}
{"count":2,"done":98304,"file":"data/train.jsonl","index":1,"total":100000,"type":"bytes"}
{"count":2,"done":100000,"file":"data/train.jsonl","index":1,"total":100000,"type":"bytes"}
{"count":2,"done":16384,"file":"data/test.jsonl","index":2,"total":40000,"type":"bytes"}
{"count":2,"done":32768,"file":"data/test.jsonl","index":2,"total":40000,"type":"bytes"}
{"count":2,"done":40000,"file":"data/test.jsonl","index":2,"total":40000,"type":"bytes"}
{"text":"/Users/someone/.apogee/training/datasets/raw/someone--heat-notes","type":"notice"}
{"text":"2 file(s), 136 KiB","type":"notice"}
{"text":"Convert it with:  apogee datasets prepare /Users/someone/.apogee/training/datasets/raw/someone--heat-notes","type":"notice"}
{"document":{"bytes":140000,"files":2,"object":"dataset.pull","path":"/Users/someone/.apogee/training/datasets/raw/someone--heat-notes","ref":"someone/heat-notes"},"type":"document"}
"""#
}
#endif
