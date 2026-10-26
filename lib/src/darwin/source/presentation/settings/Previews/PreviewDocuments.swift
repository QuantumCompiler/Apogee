#if DEBUG
/// The binary's own documents for this module's previews, copied byte for byte
/// from the recorded test fixtures named beside each (ADR 0004). A re-recorded
/// fixture is copied here again in the same change.
enum SettingsDocuments {
    /// tests/business/reads/Fixtures/models-stored-list.json
    static let modelsList = #"""
{
 "data": [
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
 "folded": 1,
 "object": "list"
}
"""#
    /// tests/business/reads/Fixtures/check-captions-skipped.json
    static let checkCaptionsSkipped = #"""
{"failures":0,"ok":true,"rows":[{"detail":"0.2.0","name":"apogee","section":"Version","status":"ok"},{"detail":"release","name":"channel","section":"Version","status":"ok"},{"detail":"/Users/someone/.apogee -- set by APOGEE_HOME","name":"root","section":"Version","status":"ok"},{"detail":"not quarantined","name":"quarantine","section":"Version","status":"ok"},{"detail":"parses cleanly","name":"config.json","section":"Config","status":"ok"},{"detail":"mock","name":"backend: planner","section":"Config","status":"ok"},{"detail":"planner","name":"default_backend","section":"Config","status":"ok"},{"detail":"ask","name":"permissions.delete_file","section":"Tools","status":"ok"},{"detail":"ask","name":"permissions.delete_note","section":"Tools","status":"ok"},{"detail":"ask","name":"permissions.edit_file","section":"Tools","status":"ok"},{"detail":"ask","name":"permissions.run_command","section":"Tools","status":"ok"},{"detail":"ask","name":"permissions.write_file","section":"Tools","status":"ok"},{"detail":"ask","name":"permissions.write_note","section":"Tools","status":"ok"},{"detail":"/Users/someone -- unset, so the folder Apogee was started in","name":"fs_root","section":"Tools","status":"ok"},{"detail":"none -- every website asks first, and a pipe or `serve` fetches nothing","name":"allowed_hosts","section":"Tools","status":"ok"},{"detail":"not configured -- no web_search tool. To add one, run SearXNG with JSON on and add under tools: search: {provider: searxng, url: http://127.0.0.1:8888}","name":"search","section":"Tools","status":"ok"},{"detail":"found on PATH","name":"git","section":"Tools","status":"ok"},{"detail":"code_exec_server.py matches the shipped copy; not registered -- it would run under /Users/someone/.apogee/training/venv/bin/python (the training environment)","name":"code-exec","remedy":"apogee mcp create code --command /Users/someone/.apogee/training/venv/bin/python --args /Users/someone/.apogee/training/scripts/code_exec_server.py","section":"MCP","status":"skipped"},{"detail":"present","name":"bundled: prompts/security-review.txt","section":"Agents","status":"ok"},{"detail":"present","name":"bundled: schemas/security-review-output.json","section":"Agents","status":"ok"},{"detail":"present","name":"bundled: prompts/release-notes.txt","section":"Agents","status":"ok"},{"detail":"present","name":"bundled: schemas/release-notes-output.json","section":"Agents","status":"ok"},{"detail":"present","name":"bundled: prompts/merge-request.txt","section":"Agents","status":"ok"},{"detail":"present","name":"bundled: schemas/merge-request-output.json","section":"Agents","status":"ok"},{"detail":"present","name":"bundled: prompts/code-review.txt","section":"Agents","status":"ok"},{"detail":"present","name":"bundled: schemas/code-review-output.json","section":"Agents","status":"ok"},{"detail":"present","name":"bundled: prompts/test-gaps.txt","section":"Agents","status":"ok"},{"detail":"present","name":"bundled: schemas/test-gaps-output.json","section":"Agents","status":"ok"},{"detail":"present","name":"bundled: prompts/doc-drift.txt","section":"Agents","status":"ok"},{"detail":"present","name":"bundled: schemas/doc-drift-output.json","section":"Agents","status":"ok"},{"detail":"present","name":"bundled: prompts/meeting-minutes.txt","section":"Agents","status":"ok"},{"detail":"present","name":"bundled: schemas/meeting-minutes-output.json","section":"Agents","status":"ok"},{"detail":"no records yet -- 'apogee knowledge capture' creates it","name":"collection: knowledge","section":"Knowledge","status":"ok"},{"detail":"none archived yet -- created by the first capture","name":"raw archive","section":"Knowledge","status":"ok"},{"detail":"found -- attached audio and video are decoded","name":"ffmpeg","section":"Attachments","status":"ok"},{"detail":"planner sees them as they are; described by planner","name":"images","section":"Attachments","status":"ok"},{"detail":"nothing here hears it -- set a transcription model with 'apogee config set-default-transcription'","name":"audio","section":"Attachments","status":"ok"},{"detail":"a timeline of its frames by planner, its sound untranscribed","name":"video","section":"Attachments","status":"ok"},{"detail":"found -- attached PDFs are read","name":"pdftotext","section":"Attachments","status":"ok"},{"detail":"found -- an attached folder inside a repository leaves out what git ignores","name":"git","section":"Attachments","status":"ok"},{"detail":"none yet -- '/attach' in a chat creates its index","name":"chat indexes","section":"Attachments","status":"ok"},{"detail":"nothing summarised yet -- a chat of two turns or more is, when it ends","name":"recall","section":"Memory","status":"ok"},{"detail":"at /Users/someone/.apogee/training/venv; sets: video, voice","name":"python env","section":"Training","status":"ok"},{"detail":"matches the shipped copy","name":"script: prepare_dataset.py","section":"Training","status":"ok"},{"detail":"matches the shipped copy","name":"script: train_mlx.py","section":"Training","status":"ok"},{"detail":"matches the shipped copy","name":"script: train_peft.py","section":"Training","status":"ok"},{"detail":"convert_hf_to_gguf.py matches the vendored copy (114 files)","name":"converter","section":"Training","status":"ok"},{"detail":"mlx fits this host, but its requirement set is not installed","name":"trainer","remedy":"apogee train setup --trainer mlx","section":"Training","status":"warn"},{"detail":"not installed -- 'train promote' needs it to convert to GGUF","name":"convert set","remedy":"apogee train setup --with convert","section":"Training","status":"warn"},{"detail":"24 eval item(s) -- The extraction role's chores in the product's own requests -- the graph clerk's entities and relations, and the shipped symphonies' fact, action and card stages -- each one JSON object held to its schema.","name":"kit: extraction-fidelity","section":"Training","status":"ok"},{"detail":"24 eval item(s) -- Follow explicit instructions precisely — format, length, and constraints.","name":"kit: instruction-following","section":"Training","status":"ok"},{"detail":"24 eval item(s) -- The knowledge-capture clerk's chore in the product's own request -- a raw discussion read into one canonical intent record: the why, the decision, its status and discipline, and the link it names.","name":"kit: knowledge-capture","section":"Training","status":"ok"},{"detail":"24 eval item(s) -- Solve arithmetic and logic problems with a correct final answer.","name":"kit: reasoning","section":"Training","status":"ok"},{"detail":"24 eval item(s) -- Emit well-formed JSON that matches a requested shape.","name":"kit: structured-output","section":"Training","status":"ok"},{"detail":"24 eval item(s) -- Condense a passage into a faithful, concise summary.","name":"kit: summarization","section":"Training","status":"ok"},{"detail":"32 eval item(s) -- Choosing the one tool call a request needs -- the right tool and arguments its schema accepts -- against the product's own file, shell and git tool declarations.","name":"kit: tool-calling","section":"Training","status":"ok"},{"detail":"24 eval item(s) -- The utility role's chores in the product's own formats -- a follow-up rewritten as a standalone search query, and a tool's output summarised for the model that acts on it.","name":"kit: utility-chores","section":"Training","status":"ok"},{"detail":"not set up -- only an mlx backend needs it (mlx-lm is not installed in the Python environment at /Users/someone/.apogee/training/venv)","name":"runtime","remedy":"apogee train setup --with mlx","section":"MLX","status":"skipped"},{"detail":"not set up -- only an MLX vision model needs it (mlx-vlm is not installed in /Users/someone/.apogee/training/venv)","name":"vision","remedy":"apogee train setup --with mlx-vlm","section":"MLX","status":"skipped"},{"detail":"not set up -- only an MLX embedding model needs it (mlx-embeddings is not installed in /Users/someone/.apogee/training/venv)","name":"embeddings","remedy":"apogee train setup --with mlx-embeddings","section":"MLX","status":"skipped"},{"detail":"not installed (opt-in) -- only 'models convert --mlx' of a Wan set and 'video generate' on MLX need it; pip builds mlx-video from GitHub at its pin, commit 87db56a51758 (2026-05-13)","name":"video","remedy":"apogee train setup --with mlx-video","section":"MLX","status":"skipped"},{"detail":"not installed (opt-in) -- only 'models convert --mlx' of a voice model and 'voice say' on MLX need it; mlx-audio's floor is >=0.4.0","name":"voice","remedy":"apogee train setup --with mlx-audio","section":"MLX","status":"skipped"},{"detail":"mlx_generate.py matches the shipped copy","name":"driver","section":"MLX","status":"ok"},{"detail":"mlx_convert.py matches the shipped copy","name":"conversion driver","section":"MLX","status":"ok"},{"detail":"mlx_embed.py matches the shipped copy","name":"embedding driver","section":"MLX","status":"ok"},{"detail":"not set up -- torch is not installed in the Python environment at /Users/someone/.apogee/training/venv","name":"runtime","remedy":"apogee train setup --with video","section":"Video","status":"skipped"},{"detail":"not set up -- torch is not installed in the Python environment at /Users/someone/.apogee/training/venv","name":"voice runtime","remedy":"apogee train setup --with voice","section":"Video","status":"skipped"},{"detail":"video_generate.py matches the shipped copy","name":"driver","section":"Video","status":"ok"},{"detail":"voice_say.py matches the shipped copy","name":"voice driver","section":"Video","status":"ok"},{"detail":"video_generate_mlx.py matches the shipped copy","name":"mlx driver","section":"Video","status":"ok"},{"detail":"voice_say_mlx.py matches the shipped copy","name":"mlx voice driver","section":"Video","status":"ok"},{"detail":"no subtitles filter (this ffmpeg was built without libass) -- a composition keeps its captions beside the video as an SRT","name":"ffmpeg captions","remedy":"brew install ffmpeg-full && brew unlink ffmpeg; brew link --force ffmpeg-full","section":"Video","status":"skipped"},{"detail":"libx264 -- clips and compositions encode as H.264","name":"ffmpeg h264","section":"Video","status":"ok"},{"detail":"/Users/someone/.apogee","name":"data directory","section":"Filesystem","status":"ok"},{"detail":"config.json and anything else the config engine owns (private)","name":"config/","section":"Filesystem","status":"ok"},{"detail":"operational log, one file per day","name":"logs/","section":"Filesystem","status":"ok"},{"detail":"persisted chat transcripts (private)","name":"sessions/","section":"Filesystem","status":"ok"},{"detail":"each chat's attached files, indexed; deleted with the chat (private)","name":"attachments/","section":"Filesystem","status":"ok"},{"detail":"summaries of past chats, searched by recall; a chat's goes with it (private)","name":"memory/","section":"Filesystem","status":"ok"},{"detail":"the model store: <model>/gguf and <model>/safetensors, one directory per set of weights (models/store.h)","name":"models/","section":"Filesystem","status":"ok"},{"detail":"vector stores for RAG","name":"embeddings/","section":"Filesystem","status":"ok"},{"detail":"notes the model keeps for you (the write_note / read_note tools)","name":"notes/","section":"Filesystem","status":"ok"},{"detail":"MCP servers scaffolded by 'apogee mcp create', one directory each","name":"mcp/","section":"Filesystem","status":"ok"},{"detail":"agents' system prompts, loaded by 'apogee analyze --agent'","name":"prompts/","section":"Filesystem","status":"ok"},{"detail":"agents' output schemas (JSON Schema), loaded beside the prompts","name":"schemas/","section":"Filesystem","status":"ok"},{"detail":"reports saved by 'apogee analyze', one directory per agent","name":"analyses/","section":"Filesystem","status":"ok"},{"detail":"symphony spec files -- the shipped starters and any of your own, played by name ('apogee symphonies')","name":"symphonies/","section":"Filesystem","status":"ok"},{"detail":"captured decision records' raw conversations (knowledge/raw) (private)","name":"knowledge/","section":"Filesystem","status":"ok"},{"detail":"training datasets, kits, scripts and the Python environment ('apogee datasets' / 'apogee train') (private)","name":"training/","section":"Filesystem","status":"ok"},{"detail":"task ledgers ('apogee task'), one directory per task, and the one-task lock (private)","name":"tasks/","section":"Filesystem","status":"ok"},{"detail":"task recipes -- the shipped starters and any of your own, run by name ('apogee task run --recipe')","name":"recipes/","section":"Filesystem","status":"ok"},{"detail":"review skills -- the shipped lenses and any of your own, each a markdown file ('apogee review skills')","name":"review-skills/","section":"Filesystem","status":"ok"},{"detail":"review sweep state -- each target's watermark, the sweep lock, and each sweep's documents ('apogee review sweep') (private)","name":"review/","section":"Filesystem","status":"ok"},{"detail":"downloads and scratch state, safe to delete","name":"cache/","section":"Filesystem","status":"ok"},{"detail":"not generated yet -- created at the first 'apogee serve'","name":"Admin token","section":"Secrets","status":"ok"},{"detail":"none -- 'apogee auth add' creates it","name":"Credential store","section":"Secrets","status":"ok"},{"detail":"kokoro, MLX, precision not stated, files whole","name":"hexgrad--Kokoro-82M/mlx/ffffffffffff","section":"Models","status":"ok"},{"detail":"WanPipeline, 4 components, files whole","name":"org--clip/pipeline/aaaaaaaaaaaa","section":"Models","status":"ok"},{"detail":"kokoro-v1_0.pth, 2 voices, files whole","name":"hexgrad--Kokoro-82M/voice/cccccccccccc","section":"Models","status":"ok"}],"warnings":2}
"""#
    /// tests/business/reads/Fixtures/models-list-server-tools.json
    static let modelsListServerTools = #"""
{"data":[{"architecture":"-","backend":"claude","backend_type":"claude-cli","group":"claude-cli","model":"","profile":"unprofiled","roles":"","section":"cloud","source":"-","state":"installed","type":"model","verified":"no turn yet"},{"architecture":"-","backend":"claude-api","backend_type":"anthropic","group":"anthropic","model":"claude-sonnet-4-5","note":"no API key found; run 'apogee auth add anthropic'","profile":"unprofiled","roles":"","section":"cloud","source":"-","state":"no key","type":"model","verified":"no turn yet"},{"architecture":"-","backend":"claude:claude-fable-5","backend_type":"claude-cli","group":"claude-cli","model":"claude-fable-5","profile":"-","roles":"","section":"cloud","source":"built in","state":"-","type":"model","verified":"reviewed 2026-10-19"},{"architecture":"-","backend":"claude:claude-fable-5-1","backend_type":"claude-cli","group":"claude-cli","model":"claude-fable-5-1","profile":"-","roles":"","section":"cloud","source":"built in","state":"-","type":"model","verified":"reviewed 2026-10-19"},{"architecture":"-","backend":"claude:claude-haiku-4-5","backend_type":"claude-cli","group":"claude-cli","model":"claude-haiku-4-5","profile":"-","roles":"","section":"cloud","source":"built in","state":"-","type":"model","verified":"reviewed 2026-10-19"},{"architecture":"-","backend":"claude:claude-haiku-5-5","backend_type":"claude-cli","group":"claude-cli","model":"claude-haiku-5-5","profile":"-","roles":"","section":"cloud","source":"built in","state":"-","type":"model","verified":"reviewed 2026-10-19"},{"architecture":"-","backend":"claude:claude-opus-4-5","backend_type":"claude-cli","group":"claude-cli","model":"claude-opus-4-5","profile":"-","roles":"","section":"cloud","source":"built in","state":"-","type":"model","verified":"reviewed 2026-10-19"},{"architecture":"-","backend":"claude:claude-opus-4-6","backend_type":"claude-cli","group":"claude-cli","model":"claude-opus-4-6","profile":"-","roles":"","section":"cloud","source":"built in","state":"-","type":"model","verified":"reviewed 2026-10-19"},{"architecture":"-","backend":"claude:claude-opus-4-7","backend_type":"claude-cli","group":"claude-cli","model":"claude-opus-4-7","profile":"-","roles":"","section":"cloud","source":"built in","state":"-","type":"model","verified":"reviewed 2026-10-19"},{"architecture":"-","backend":"claude:claude-opus-4-8","backend_type":"claude-cli","group":"claude-cli","model":"claude-opus-4-8","profile":"-","roles":"","section":"cloud","source":"built in","state":"-","type":"model","verified":"reviewed 2026-10-19"},{"architecture":"-","backend":"claude:claude-opus-5","backend_type":"claude-cli","group":"claude-cli","model":"claude-opus-5","profile":"-","roles":"","section":"cloud","source":"built in","state":"-","type":"model","verified":"reviewed 2026-10-19"},{"architecture":"-","backend":"claude:claude-opus-5-5","backend_type":"claude-cli","group":"claude-cli","model":"claude-opus-5-5","profile":"-","roles":"","section":"cloud","source":"built in","state":"-","type":"model","verified":"reviewed 2026-10-19"},{"architecture":"-","backend":"claude:claude-sonnet-4-6","backend_type":"claude-cli","group":"claude-cli","model":"claude-sonnet-4-6","profile":"-","roles":"","section":"cloud","source":"built in","state":"-","type":"model","verified":"reviewed 2026-10-19"},{"architecture":"-","backend":"claude:claude-sonnet-5","backend_type":"claude-cli","group":"claude-cli","model":"claude-sonnet-5","profile":"-","roles":"","section":"cloud","source":"built in","state":"-","type":"model","verified":"reviewed 2026-10-19"},{"architecture":"-","backend":"claude:claude-sonnet-5-5","backend_type":"claude-cli","group":"claude-cli","model":"claude-sonnet-5-5","profile":"-","roles":"","section":"cloud","source":"built in","state":"-","type":"model","verified":"reviewed 2026-10-19"},{"architecture":"-","backend":"gpt","backend_type":"openai","group":"openai","model":"gpt-5","note":"no API key found; run 'apogee auth add openai'","profile":"unprofiled","roles":"","section":"cloud","source":"-","state":"no key","type":"model","verified":"no turn yet"}],"folded":0,"object":"list"}
"""#
    /// tests/business/reads/Fixtures/providers-scan.json
    static let providersScan = #"""
{"data":[{"backend":"not registered","evidence":["/Users/someone/.local/bin/claude, 2.1.289 (Claude Code)","no ~/.claude.json"],"label":"Claude CLI","provider":"claude","tier":"installed","type":"claude-cli"},{"backend":"not registered","evidence":["/Users/someone/.local/bin/codex, codex-cli 0.153.4","`codex login status` reports no login"],"label":"Codex CLI","provider":"codex","tier":"installed","type":"codex-cli"},{"backend":"not registered","evidence":["/opt/homebrew/bin/gemini, 0.46.0","no ~/.gemini/google_accounts.json"],"label":"Gemini CLI","provider":"gemini","tier":"installed","type":"gemini-cli"},{"backend":"not registered","evidence":["/usr/local/bin/ollama, ollama version is 0.40.6","the Ollama CLI leaves no login evidence Apogee can check"],"label":"Ollama CLI","provider":"ollama","tier":"installed","type":"ollama-cli"},{"backend":"","evidence":["no key resolves"],"label":"Anthropic API","provider":"anthropic","tier":"not found","type":"anthropic"},{"backend":"","evidence":["no key resolves"],"label":"OpenAI API","provider":"openai","tier":"not found","type":"openai"},{"backend":"","evidence":["no key resolves"],"label":"Google API","provider":"google","tier":"not found","type":"google"}],"object":"list"}
"""#
    /// tests/business/reads/Fixtures/config-get-server-tools.txt
    static let configGetServerTools = #"""
web_search
code_execution
"""#
    /// tests/business/reads/Fixtures/config-get-server-tools-none.txt
    static let configGetServerToolsNone = #"""

"""#
    /// tests/business/reads/Fixtures/config-get-server-tools-older.err
    static let configGetServerToolsOlder = #"""
apogee config: no such config key: 'backends.claude-api.server_tools'
"""#
    /// tests/business/reads/Fixtures/config-get-server-tools-older-gpt.err
    static let configGetServerToolsOlderGpt = #"""
apogee config: no such config key: 'backends.gpt.server_tools'
"""#
    /// tests/business/reads/Fixtures/chats-recall.txt
    static let chatsRecall = #"""
recall holds 2 chat summaries in /Users/someone/.apogee/memory/chats.db
"""#
}
#endif
