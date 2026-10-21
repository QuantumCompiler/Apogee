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
}
#endif
