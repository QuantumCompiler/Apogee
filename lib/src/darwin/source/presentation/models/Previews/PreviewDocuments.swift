#if DEBUG
/// The binary's own documents for this module's previews, copied byte for byte
/// from the recorded test fixtures named beside each (ADR 0004). A re-recorded
/// fixture is copied here again in the same change.
enum ModelsDocuments {
    /// tests/business/reads/Fixtures/store-list.json
    static let storeList = #"""
{"data":[{"architecture":"llama","backend":"(not configured)","backend_type":"-","format":"gguf","group":"gguf","model":"Llama3.2-1B-Instruct/gguf/3f0c2a9b7e41","note":"Llama3.2-1B-Instruct-F16.gguf","profile":"llama3 (unverified)","roles":"","section":"local","source":"converted","state":"ok","type":"model","verified":"no digest published, header ok"},{"architecture":"gemma4","backend":"(not configured)","backend_type":"-","format":"mlx","group":"mlx","model":"Gemma4-26B-Instruct/mlx/a88b9f386aff","note":"gemma4, mixed 4/8-bit (group 64), 32768-token window (the default; trained for 262144), 13.2 GiB","profile":"gemma3","quant":"mixed 4/8-bit (group 64)","roles":"","section":"local","source":"converted","state":"mlx","type":"model","verified":"10 file(s) on record","window":"32768-token window (the default; trained for 262144)"},{"architecture":"kokoro-v1_0.pth","backend":"(not configured)","backend_type":"-","bytes":1028,"format":"voice","group":"voice","model":"Kokoro-82M/voice/5f3a2c91d0e4","note":"kokoro-v1_0.pth, 3 voices (American English, British English), 1 KiB","profile":"-","roles":"","section":"local","source":"local","state":"voice","type":"model","verified":"no record"},{"architecture":"llama","backend":"llama3.2-1b-Q4KM","backend_type":"llamacpp","format":"gguf","group":"gguf","model":"Llama3.2-1B-Instruct-Q4KM.gguf","profile":"llama3 (unverified)","roles":"","section":"local","source":"converted","state":"ok","type":"model","verified":"no digest published, header ok"}],"folded":2,"object":"list"}
"""#
    /// tests/business/reads/Fixtures/store-list-all.json
    static let storeListAll = #"""
{"data":[{"architecture":"llama","backend":"(not configured)","backend_type":"-","format":"gguf","group":"gguf","model":"Llama3.2-1B-Instruct/gguf/3f0c2a9b7e41","note":"Llama3.2-1B-Instruct-F16.gguf","profile":"llama3 (unverified)","roles":"","section":"local","source":"converted","state":"ok","type":"model","verified":"no digest published, header ok"},{"architecture":"Gemma4","backend":"(not configured)","backend_type":"-","format":"safetensors","group":"safetensors","model":"Gemma4-26B-Instruct/safetensors/13de95725073","profile":"-","roles":"","section":"local","source":"huggingface","state":"safetensors","type":"model","verified":"9 file(s) on record"},{"architecture":"Llama","backend":"(not configured)","backend_type":"-","format":"safetensors","group":"safetensors","model":"Llama3.2-1B-Instruct/safetensors/edaba290ce7f","profile":"-","roles":"","section":"local","source":"huggingface","state":"safetensors","type":"model","verified":"9 file(s) on record"},{"architecture":"gemma4","backend":"(not configured)","backend_type":"-","format":"mlx","group":"mlx","model":"Gemma4-26B-Instruct/mlx/a88b9f386aff","note":"gemma4, mixed 4/8-bit (group 64), 32768-token window (the default; trained for 262144), 13.2 GiB","profile":"gemma3","quant":"mixed 4/8-bit (group 64)","roles":"","section":"local","source":"converted","state":"mlx","type":"model","verified":"10 file(s) on record","window":"32768-token window (the default; trained for 262144)"},{"architecture":"kokoro-v1_0.pth","backend":"(not configured)","backend_type":"-","bytes":1028,"format":"voice","group":"voice","model":"Kokoro-82M/voice/5f3a2c91d0e4","note":"kokoro-v1_0.pth, 3 voices (American English, British English), 1 KiB","profile":"-","roles":"","section":"local","source":"local","state":"voice","type":"model","verified":"no record"},{"architecture":"llama","backend":"llama3.2-1b-Q4KM","backend_type":"llamacpp","format":"gguf","group":"gguf","model":"Llama3.2-1B-Instruct-Q4KM.gguf","profile":"llama3 (unverified)","roles":"","section":"local","source":"converted","state":"ok","type":"model","verified":"no digest published, header ok"}],"folded":0,"object":"list"}
"""#
    /// tests/business/reads/Fixtures/store-info-backend.json
    static let storeInfoBackend = #"""
{"fields":[{"field":"backend","value":"llama3.2-1b-Q4KM"},{"field":"type","value":"llamacpp"},{"field":"roles","value":"-"},{"field":"model_path","value":"/Users/someone/.apogee/models/Llama3.2-1B-Instruct/gguf/8d51e6c04a27/Llama3.2-1B-Instruct-Q4KM.gguf"},{"field":"lineage","value":"quantized to Q4_K_M from Llama3.2-1B-Instruct/gguf/3f0c2a9b7e41 (recorded)\nconverted from Llama3.2-1B-Instruct/safetensors/edaba290ce7f (recorded)\npulled from meta-llama/Llama-3.2-1B-Instruct (Hugging Face)"},{"field":"header","value":"ok (GGUF v3)"},{"field":"architecture","value":"llama"},{"field":"profile","value":"llama3 (unverified)"},{"field":"evidence","value":"not verified: the llama3.2 files available carry no chat template and degenerate on every prompt -- likely base models. Registered so the family is a declared gap, not a silent one"},{"field":"name","value":"Llama 3.2 1B Instruct"},{"field":"tensors","value":"1 total, 1 text"},{"field":"size","value":"770 MiB"},{"field":"template","value":"none, so it is most likely a base (pretrained) model: it continues text rather than answering, so what it says can be confidently wrong, and it cannot use tools. For chat, use its instruction-tuned release, usually named '-it' or '-Instruct'"},{"field":"window","value":"32768 tokens (the default; trained for 131072)"},{"field":"cache","value":"544 MiB at q8_0 (the default)"},{"field":"sampling","value":"temperature 0.6 (family) · top-p 0.9 (family) · top-k 0 (default) · min-p 0 (default) · repeat penalty 1 (default) · presence penalty 0 (default) · seed drawn per answer -- family values from the Llama 3.1 and 3.2 Instruct generation config: temperature 0.6, top-p 0.9"},{"field":"thinking","value":"on (default), no budget -- off renders the template's own switch, where it has one"}],"name":"llama3.2-1b-Q4KM"}
"""#
    /// tests/business/reads/Fixtures/store-info-q4.json
    static let storeInfoQ4 = #"""
{"data":{"architecture":"llama","bytes":807694988,"cache":{"bytes":570425344,"sliding_positions":0,"type":"q8_0"},"format":"gguf","handle":"Llama3.2-1B-Instruct/gguf/8d51e6c04a27","lineage":[{"detail":"Q4_K_M","from":"Llama3.2-1B-Instruct/gguf/3f0c2a9b7e41","inferred":false,"kind":"quantized","missing":false},{"detail":"","from":"Llama3.2-1B-Instruct/safetensors/edaba290ce7f","inferred":false,"kind":"converted","missing":false},{"detail":"huggingface","from":"meta-llama/Llama-3.2-1B-Instruct","inferred":false,"kind":"pulled","missing":false}],"window":{"source":"default","tokens":32768,"trained":131072}},"fields":[{"field":"weights","value":"Llama3.2-1B-Instruct/gguf/8d51e6c04a27"},{"field":"backends","value":"llama3.2-1b-Q4KM"},{"field":"model_path","value":"/Users/someone/.apogee/models/Llama3.2-1B-Instruct/gguf/8d51e6c04a27/Llama3.2-1B-Instruct-Q4KM.gguf"},{"field":"lineage","value":"quantized to Q4_K_M from Llama3.2-1B-Instruct/gguf/3f0c2a9b7e41 (recorded)\nconverted from Llama3.2-1B-Instruct/safetensors/edaba290ce7f (recorded)\npulled from meta-llama/Llama-3.2-1B-Instruct (Hugging Face)"},{"field":"header","value":"ok (GGUF v3)"},{"field":"architecture","value":"llama"},{"field":"profile","value":"llama3 (unverified)"},{"field":"evidence","value":"not verified: the llama3.2 files available carry no chat template and degenerate on every prompt -- likely base models. Registered so the family is a declared gap, not a silent one"},{"field":"name","value":"Llama 3.2 1B Instruct"},{"field":"tensors","value":"1 total, 1 text"},{"field":"size","value":"770 MiB"},{"field":"template","value":"none, so it is most likely a base (pretrained) model: it continues text rather than answering, so what it says can be confidently wrong, and it cannot use tools. For chat, use its instruction-tuned release, usually named '-it' or '-Instruct'"},{"field":"window","value":"32768 tokens (the default; trained for 131072)"},{"field":"cache","value":"544 MiB at q8_0 (the default)"},{"field":"sampling","value":"temperature 0.6 (family) · top-p 0.9 (family) · top-k 0 (default) · min-p 0 (default) · repeat penalty 1 (default) · presence penalty 0 (default) · seed drawn per answer -- family values from the Llama 3.1 and 3.2 Instruct generation config: temperature 0.6, top-p 0.9"},{"field":"thinking","value":"on (default), no budget -- no control here: this backend thinks as it decides"}],"name":"/Users/someone/.apogee/models/Llama3.2-1B-Instruct/gguf/8d51e6c04a27/Llama3.2-1B-Instruct-Q4KM.gguf"}
"""#
    /// tests/business/reads/Fixtures/store-info-f16.json
    static let storeInfoF16 = #"""
{"data":{"architecture":"llama","bytes":2479596089,"cache":{"bytes":570425344,"sliding_positions":0,"type":"q8_0"},"format":"gguf","handle":"Llama3.2-1B-Instruct/gguf/3f0c2a9b7e41","lineage":[{"detail":"","from":"Llama3.2-1B-Instruct/safetensors/edaba290ce7f","inferred":false,"kind":"converted","missing":false},{"detail":"huggingface","from":"meta-llama/Llama-3.2-1B-Instruct","inferred":false,"kind":"pulled","missing":false}],"window":{"source":"default","tokens":32768,"trained":131072}},"fields":[{"field":"weights","value":"Llama3.2-1B-Instruct/gguf/3f0c2a9b7e41"},{"field":"backends","value":"none -- 'apogee config add-backend <name> --type llamacpp --model-path <the path below>'"},{"field":"model_path","value":"/Users/someone/.apogee/models/Llama3.2-1B-Instruct/gguf/3f0c2a9b7e41/Llama3.2-1B-Instruct-F16.gguf"},{"field":"lineage","value":"converted from Llama3.2-1B-Instruct/safetensors/edaba290ce7f (recorded)\npulled from meta-llama/Llama-3.2-1B-Instruct (Hugging Face)"},{"field":"header","value":"ok (GGUF v3)"},{"field":"architecture","value":"llama"},{"field":"profile","value":"llama3 (unverified)"},{"field":"evidence","value":"not verified: the llama3.2 files available carry no chat template and degenerate on every prompt -- likely base models. Registered so the family is a declared gap, not a silent one"},{"field":"name","value":"Llama 3.2 1B Instruct"},{"field":"tensors","value":"1 total, 1 text"},{"field":"size","value":"2364 MiB"},{"field":"template","value":"none, so it is most likely a base (pretrained) model: it continues text rather than answering, so what it says can be confidently wrong, and it cannot use tools. For chat, use its instruction-tuned release, usually named '-it' or '-Instruct'"},{"field":"window","value":"32768 tokens (the default; trained for 131072)"},{"field":"cache","value":"544 MiB at q8_0 (the default)"},{"field":"sampling","value":"temperature 0.6 (family) · top-p 0.9 (family) · top-k 0 (default) · min-p 0 (default) · repeat penalty 1 (default) · presence penalty 0 (default) · seed drawn per answer -- family values from the Llama 3.1 and 3.2 Instruct generation config: temperature 0.6, top-p 0.9"},{"field":"thinking","value":"on (default), no budget -- no control here: this backend thinks as it decides"}],"name":"Llama3.2-1B-Instruct/gguf/3f0c2a9b7e41"}
"""#
    /// tests/business/reads/Fixtures/store-info-snapshot.json
    static let storeInfoSnapshot = #"""
{"data":{"architecture":"Llama","bytes":2482980600,"cache":null,"format":"safetensors","handle":"Llama3.2-1B-Instruct/safetensors/edaba290ce7f","lineage":[{"detail":"huggingface","from":"meta-llama/Llama-3.2-1B-Instruct","inferred":false,"kind":"pulled","missing":false}],"window":null},"fields":[{"field":"weights","value":"Llama3.2-1B-Instruct/safetensors/edaba290ce7f"},{"field":"path","value":"/Users/someone/.apogee/models/Llama3.2-1B-Instruct/safetensors/edaba290ce7f"},{"field":"format","value":"SafeTensors -- full weights, trainable, not runnable"},{"field":"lineage","value":"pulled from meta-llama/Llama-3.2-1B-Instruct (Hugging Face)"},{"field":"architecture","value":"Llama"},{"field":"record","value":"9 file(s) on record"},{"field":"made from it","value":"Llama3.2-1B-Instruct/gguf/3f0c2a9b7e41 (recorded)\nLlama3.2-1B-Instruct/gguf/8d51e6c04a27 (recorded)"},{"field":"listing","value":"folded -- a conversion consumed it; 'apogee models list --all' shows it"}],"name":"Llama3.2-1B-Instruct/safetensors/edaba290ce7f"}
"""#
    /// tests/business/reads/Fixtures/store-info-mlx.json
    static let storeInfoMLX = #"""
{"data":{"architecture":"Gemma4ForConditionalGeneration","bytes":14232576040,"cache":null,"format":"mlx","handle":"Gemma4-26B-Instruct/mlx/a88b9f386aff","lineage":[{"detail":"convert","from":"Gemma4-26B-Instruct/safetensors/13de95725073","inferred":false,"kind":"converted","missing":false}],"window":{"source":"default","tokens":32768,"trained":262144}},"fields":[{"field":"weights","value":"Gemma4-26B-Instruct/mlx/a88b9f386aff"},{"field":"backends","value":"none -- 'apogee config add-backend Gemma4-26B-Instruct-mixed_4_8 --type mlx --model-path <the path below>'"},{"field":"model_path","value":"/Users/someone/.apogee/models/Gemma4-26B-Instruct/mlx/a88b9f386aff"},{"field":"format","value":"MLX -- runs on an mlx backend (Apple silicon)"},{"field":"lineage","value":"converted from Gemma4-26B-Instruct/safetensors/13de95725073 (recorded, mlx_lm.convert 4bit)"},{"field":"model_type","value":"gemma4"},{"field":"template","value":"ships with the model"},{"field":"files","value":"whole -- config.json, a tokenizer and 3 shard(s), each holding every byte its header lists"},{"field":"quantization","value":"mixed 4/8-bit (group 64)"},{"field":"window","value":"32768 tokens (the default; trained for 262144)"},{"field":"size","value":"13.2 GiB on disk"}],"name":"Gemma4-26B-Instruct/mlx/a88b9f386aff"}
"""#
    /// tests/business/reads/Fixtures/models-stored-info-voice.json
    static let storedInfoVoice = #"""
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
}
#endif
