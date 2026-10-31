#if DEBUG
/// The binary's own documents for this module's previews, copied byte for byte
/// from the recorded test fixtures named beside each (ADR 0004). A re-recorded
/// fixture is copied here again in the same change.
enum VoiceDocuments {
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
    /// tests/business/reads/Fixtures/models-stored-info-voice.json
    static let voiceInfo = #"""
{
 "data": {
  "architecture": null,
  "bytes": 1028,
  "cache": null,
  "format": "voice",
  "handle": "Kokoro-82M/voice/5f3a2c91d0e4",
  "lineage": [],
  "window": null
 },
 "fields": [
  {
   "field": "weights",
   "value": "Kokoro-82M/voice/5f3a2c91d0e4"
  },
  {
   "field": "path",
   "value": "/Users/someone/.apogee/models/Kokoro-82M/voice/5f3a2c91d0e4"
  },
  {
   "field": "format",
   "value": "voice -- a text-to-speech model, taken whole"
  },
  {
   "field": "lineage",
   "value": "unknown -- no record says where it came from"
  },
  {
   "field": "files",
   "value": "whole -- config.json, kokoro-v1_0.pth and 3 voice(s), each a whole archive"
  },
  {
   "field": "voices",
   "value": "American English: af_heart, am_adam\nBritish English: bf_emma"
  },
  {
   "field": "size",
   "value": "1 KiB on disk"
  }
 ],
 "name": "Kokoro-82M/voice/5f3a2c91d0e4"
}
"""#
    /// tests/business/reads/Fixtures/media-list.json
    static let mediaList = #"""
{"data":[{"architecture":"kokoro","backend":"(not configured)","backend_type":"-","format":"mlx","group":"mlx","kind":"voice","model":"Kokoro-82M/mlx/6f708192a3b4","note":"a voice model: 'apogee voice say' speaks with it, never a chat backend","profile":"unprofiled","quant":"precision not stated","roles":"","section":"local","source":"converted","state":"mlx","type":"model","verified":"5 file(s) on record","window":"-"},{"architecture":"ti2v","backend":"(not configured)","backend_type":"-","format":"mlx","group":"mlx","kind":"video","model":"Wan2.2-TI2V-5B/mlx/5e6f708192a3","note":"a video model: 'apogee video generate' runs it, never a chat backend","profile":"unprofiled","quant":"4-bit (affine, group 64)","roles":"","section":"local","source":"converted","state":"mlx","type":"model","verified":"4 file(s) on record","window":"-"},{"architecture":"WanPipeline","backend":"(not configured)","backend_type":"-","bytes":462,"components":4,"format":"pipeline","group":"pipeline","model":"Wan2.1-T2V-1.3B/pipeline/7c21e0d4b9a3","note":"WanPipeline, 4 components (scheduler, tokenizer, transformer, vae), 2 shard(s), 462 B","profile":"-","roles":"","section":"local","source":"local","state":"pipeline","type":"model","verified":"no record"},{"architecture":"WanPipeline","backend":"(not configured)","backend_type":"-","bytes":462,"components":4,"format":"pipeline","group":"pipeline","model":"Wan2.2-TI2V-5B/pipeline/9b1e47c0a2f6","note":"WanPipeline, 4 components (scheduler, tokenizer, transformer, vae), 2 shard(s), 462 B","profile":"-","roles":"","section":"local","source":"local","state":"pipeline","type":"model","verified":"no record"},{"architecture":"kokoro-v1_0.pth","backend":"(not configured)","backend_type":"-","bytes":1498,"format":"voice","group":"voice","model":"Kokoro-82M/voice/5f3a2c91d0e4","note":"kokoro-v1_0.pth, 3 voices (American English, British English), 1 KiB","profile":"-","roles":"","section":"local","source":"huggingface","state":"voice","type":"model","verified":"5 file(s) on record"}],"folded":1,"object":"list"}
"""#
    /// tests/business/reads/Fixtures/media-info-voice-torch.json
    static let voiceInfoTorch = #"""
{"data":{"architecture":null,"bytes":1498,"cache":null,"converts_to":null,"format":"voice","handle":"Kokoro-82M/voice/5f3a2c91d0e4","kind":null,"lineage":[],"mlx_conversion":null,"vector_width":null,"wan_layout":null,"window":null},"fields":[{"field":"weights","value":"Kokoro-82M/voice/5f3a2c91d0e4"},{"field":"path","value":"/Users/someone/.apogee/models/Kokoro-82M/voice/5f3a2c91d0e4"},{"field":"format","value":"voice -- a text-to-speech model, taken whole"},{"field":"lineage","value":"pulled from hexgrad/Kokoro-82M (Hugging Face)"},{"field":"files","value":"whole -- config.json, kokoro-v1_0.pth and 3 voice(s), each a whole archive"},{"field":"voices","value":"American English: af_heart, am_adam\nBritish English: bf_emma"},{"field":"size","value":"1 KiB on disk"},{"field":"made from it","value":"Kokoro-82M/mlx/6f708192a3b4 (recorded)"}],"name":"Kokoro-82M/voice/5f3a2c91d0e4"}
"""#
    /// tests/business/reads/Fixtures/media-info-voice-mlx.json
    static let voiceInfoMLX = #"""
{"data":{"architecture":null,"bytes":1867,"cache":null,"converts_to":null,"format":"mlx","handle":"Kokoro-82M/mlx/6f708192a3b4","kind":"voice","lineage":[{"detail":"convert","from":"Kokoro-82M/voice/5f3a2c91d0e4","inferred":false,"kind":"converted","missing":false}],"mlx_conversion":null,"vector_width":null,"wan_layout":null,"window":null},"fields":[{"field":"weights","value":"Kokoro-82M/mlx/6f708192a3b4"},{"field":"backends","value":"none -- no chat backend runs it; the kind line says what does"},{"field":"model_path","value":"/Users/someone/.apogee/models/Kokoro-82M/mlx/6f708192a3b4"},{"field":"format","value":"MLX (Apple silicon)"},{"field":"kind","value":"voice (recorded) -- a voice model: 'apogee voice say' speaks with it, never a chat backend"},{"field":"voices","value":"American English: af_heart, am_adam\nBritish English: bf_emma"},{"field":"lineage","value":"converted from Kokoro-82M/voice/5f3a2c91d0e4 (recorded, mlx_audio.convert bf16)"},{"field":"model_type","value":"kokoro"},{"field":"files","value":"whole -- config.json, the weights and its voices (voices/*.safetensors), 1 shard(s), each holding every byte its header lists"},{"field":"quantization","value":"precision not stated"},{"field":"size","value":"1 KiB on disk"}],"name":"Kokoro-82M/mlx/6f708192a3b4"}
"""#
    /// tests/business/media/Fixtures/speech-torch.json
    static let speechTorch = #"""
{"created_at":"2026-10-22T09:05:50Z","device":{"device":"cpu","platform":"macos-arm64"},"engine":"torch","language":"American English","model":{"checkpoint":"kokoro-v1_0.pth","name":"Kokoro-82M/voice/5f3a2c91d0e4","path":"/Users/someone/.apogee/models/Kokoro-82M/voice/5f3a2c91d0e4","ref":"hexgrad/Kokoro-82M","revision":"main"},"object":"voice.speech","output":"/Users/someone/Music/Apogee/hello-pytorch.wav","run_seconds":0.091,"sample_rate":24000,"seconds":0.1,"segments":[{"offset":0.0,"seconds":0.1,"sentences":[{"offset":0.0,"seconds":0.05,"text":"Hello there."},{"offset":0.05,"seconds":0.05,"text":"How are you?"}],"text":"Hello there. How are you?"}],"sidecar":"/Users/someone/Music/Apogee/hello-pytorch.wav.json","speed":1.0,"status":"done","text":"Hello there. How are you?","versions":{"apogee":"0.1.9","kokoro":"0.0-stub","torch":"0.0-stub"},"voice":"af_heart"}
"""#
    /// tests/business/media/Fixtures/speech-mlx.json
    static let speechMLX = #"""
{"created_at":"2026-10-22T09:05:50Z","device":{"device":"gpu","platform":"macos-arm64"},"engine":"mlx","language":"American English","model":{"checkpoint":"model.safetensors","name":"Kokoro-82M/mlx/6f708192a3b4","path":"/Users/someone/.apogee/models/Kokoro-82M/mlx/6f708192a3b4","ref":"Kokoro-82M/voice/5f3a2c91d0e4","revision":""},"object":"voice.speech","output":"/Users/someone/Music/Apogee/hello-mlx.wav","run_seconds":0.108,"sample_rate":24000,"seconds":0.1,"segments":[{"offset":0.0,"seconds":0.1,"sentences":[{"offset":0.0,"seconds":0.05,"text":"Hello there."},{"offset":0.05,"seconds":0.05,"text":"How are you?"}],"text":"Hello there. How are you?"}],"sidecar":"/Users/someone/Music/Apogee/hello-mlx.wav.json","speed":1.0,"status":"done","text":"Hello there. How are you?","versions":{"apogee":"0.1.9","kokoro":"","mlx":"0.31.2","mlx_audio":"0.0-audio-stub","torch":""},"voice":"af_heart"}
"""#
    /// tests/business/media/Fixtures/voice-refused-runtime.jsonl
    static let voiceRefused = #"""
{"protocol_version":1,"schema":"2026-10-25","type":"job","verb":"voice say"}
"""#
    /// tests/business/media/Fixtures/voice-refused-runtime.err
    static let voiceRefusedErr = #"""
apogee voice say: torch and kokoro are not installed in Apogee's Python environment -- apogee train setup --with voice
"""#
    /// tests/business/media/Fixtures/install-voice-done.txt
    static let installVoiceDone = #"""
environment already exists at /Users/someone/.apogee/training/venv
installing the 'voice' set: torch>=2.6 kokoro>=0.9.4 misaki[en]>=0.9.4 ...
installed

python env: at /Users/someone/.apogee/training/venv; sets: video, voice
"""#
    /// tests/business/media/Fixtures/install-voice-failed.txt
    static let installVoiceFailed = #"""
environment already exists at /Users/someone/.apogee/training/venv
installing the 'voice' set: torch>=2.6 kokoro>=0.9.4 misaki[en]>=0.9.4 ...
"""#
    /// tests/business/media/Fixtures/install-voice-failed.err
    static let installVoiceFailedErr = #"""
apogee train: pip install for the 'voice' set failed (exit code 1):
ERROR: Could not install packages: this sandbox's pip is a stub with no network
"""#
    /// tests/business/media/Fixtures/voice-refused-runtime-m29.jsonl
    static let voiceRefusedM29 = #"""
{"protocol_version":1,"schema":"2026-10-26","type":"job","verb":"voice say"}
{"message":"torch and kokoro are not installed in Apogee's Python environment -- apogee train setup --with voice","type":"error"}
"""#
    /// tests/business/media/Fixtures/voice-refused-runtime-m29.err
    static let voiceRefusedM29Err = #"""
apogee voice say: torch and kokoro are not installed in Apogee's Python environment -- apogee train setup --with voice
"""#
}
#endif
