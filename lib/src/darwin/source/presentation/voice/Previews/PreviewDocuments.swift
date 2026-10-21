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
}
#endif
