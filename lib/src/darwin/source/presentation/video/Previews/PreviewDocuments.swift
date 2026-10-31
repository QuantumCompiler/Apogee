#if DEBUG
/// The binary's own documents for this module's previews, copied byte for byte
/// from the recorded test fixtures named beside each (ADR 0004). A re-recorded
/// fixture is copied here again in the same change.
enum VideoDocuments {
    /// tests/business/reads/Fixtures/media-list.json
    static let mediaList = #"""
{"data":[{"architecture":"kokoro","backend":"(not configured)","backend_type":"-","format":"mlx","group":"mlx","kind":"voice","model":"Kokoro-82M/mlx/6f708192a3b4","note":"a voice model: 'apogee voice say' speaks with it, never a chat backend","profile":"unprofiled","quant":"precision not stated","roles":"","section":"local","source":"converted","state":"mlx","type":"model","verified":"5 file(s) on record","window":"-"},{"architecture":"ti2v","backend":"(not configured)","backend_type":"-","format":"mlx","group":"mlx","kind":"video","model":"Wan2.2-TI2V-5B/mlx/5e6f708192a3","note":"a video model: 'apogee video generate' runs it, never a chat backend","profile":"unprofiled","quant":"4-bit (affine, group 64)","roles":"","section":"local","source":"converted","state":"mlx","type":"model","verified":"4 file(s) on record","window":"-"},{"architecture":"WanPipeline","backend":"(not configured)","backend_type":"-","bytes":462,"components":4,"format":"pipeline","group":"pipeline","model":"Wan2.1-T2V-1.3B/pipeline/7c21e0d4b9a3","note":"WanPipeline, 4 components (scheduler, tokenizer, transformer, vae), 2 shard(s), 462 B","profile":"-","roles":"","section":"local","source":"local","state":"pipeline","type":"model","verified":"no record"},{"architecture":"WanPipeline","backend":"(not configured)","backend_type":"-","bytes":462,"components":4,"format":"pipeline","group":"pipeline","model":"Wan2.2-TI2V-5B/pipeline/9b1e47c0a2f6","note":"WanPipeline, 4 components (scheduler, tokenizer, transformer, vae), 2 shard(s), 462 B","profile":"-","roles":"","section":"local","source":"local","state":"pipeline","type":"model","verified":"no record"},{"architecture":"kokoro-v1_0.pth","backend":"(not configured)","backend_type":"-","bytes":1498,"format":"voice","group":"voice","model":"Kokoro-82M/voice/5f3a2c91d0e4","note":"kokoro-v1_0.pth, 3 voices (American English, British English), 1 KiB","profile":"-","roles":"","section":"local","source":"huggingface","state":"voice","type":"model","verified":"5 file(s) on record"}],"folded":1,"object":"list"}
"""#
    /// tests/business/reads/Fixtures/media-default-video.txt
    static let defaultVideo = #"""
Wan2.2-TI2V-5B/mlx/5e6f708192a3
"""#
    /// tests/business/reads/Fixtures/media-default-video-unset.txt
    static let defaultVideoUnset = #"""

"""#
    /// tests/business/media/Fixtures/clip-torch.json
    static let clipTorch = #"""
{"codec":"h264","created_at":"2026-10-22T09:05:50Z","device":{"device":"cpu","dtype":"float32","platform":"macos-arm64"},"engine":"torch","image":null,"loras":[],"mode":"t2v","model":{"class":"WanPipeline","name":"Wan2.2-TI2V-5B/pipeline/9b1e47c0a2f6","path":"/Users/someone/.apogee/models/Wan2.2-TI2V-5B/pipeline/9b1e47c0a2f6","ref":"","revision":"","variant":null},"object":"video.generation","output":"/Users/someone/Movies/Apogee/lighthouse-pytorch.mp4","parent":null,"preset":null,"prompt":"A lighthouse at dusk","seconds":0.179,"seed":1095329999,"seed_source":"generated","settings":{"duration":1.0,"fps":8,"frames":9,"guidance":null,"height":144,"steps":4,"width":256},"sidecar":"/Users/someone/Movies/Apogee/lighthouse-pytorch.mp4.json","status":"done","versions":{"apogee":"0.1.9","diffusers":"0.0-stub","torch":"0.0-stub"}}
"""#
    /// tests/business/media/Fixtures/clip-mlx.json
    static let clipMLX = #"""
{"codec":"h264","created_at":"2026-10-22T09:05:50Z","device":{"device":"gpu","dtype":"4-bit","platform":"macos-arm64"},"engine":"mlx","image":null,"loras":[],"mode":"t2v","model":{"class":"Wan ti2v","name":"Wan2.2-TI2V-5B/mlx/5e6f708192a3","path":"/Users/someone/.apogee/models/Wan2.2-TI2V-5B/mlx/5e6f708192a3","ref":"Wan-AI--Wan2.2-TI2V-5B/safetensors/6ef9b00b8b83","revision":"","variant":null},"object":"video.generation","output":"/Users/someone/Movies/Apogee/lighthouse-mlx.mp4","parent":null,"preset":null,"prompt":"A lighthouse at dusk","seconds":0.093,"seed":88280263,"seed_source":"generated","settings":{"duration":1.0,"fps":8,"frames":9,"guidance":null,"height":144,"steps":4,"width":256},"sidecar":"/Users/someone/Movies/Apogee/lighthouse-mlx.mp4.json","status":"done","versions":{"apogee":"0.1.9","diffusers":"","mlx":"0.31.2","mlx_video":"0.0-video-stub","torch":""}}
"""#
    /// tests/business/media/Fixtures/generate-refused-runtime.jsonl
    static let generateRefused = #"""
{"protocol_version":1,"schema":"2026-10-25","type":"job","verb":"video generate"}
"""#
    /// tests/business/media/Fixtures/generate-refused-runtime.err
    static let generateRefusedErr = #"""
apogee video generate: torch and diffusers are not installed in Apogee's Python environment -- apogee train setup --with video
"""#
    /// tests/business/media/Fixtures/install-video-done.txt
    static let installVideoDone = #"""
environment already exists at /Users/someone/.apogee/training/venv
installing the 'video' set: torch>=2.6 diffusers>=0.35 transformers>=4.46 accelerate>=1.0 ftfy>=6.1 sentencepiece>=0.2 peft>=0.13 ...
installed

python env: at /Users/someone/.apogee/training/venv; sets: video
"""#
    /// tests/business/media/Fixtures/install-video-failed.txt
    static let installVideoFailed = #"""
environment already exists at /Users/someone/.apogee/training/venv
installing the 'video' set: torch>=2.6 diffusers>=0.35 transformers>=4.46 accelerate>=1.0 ftfy>=6.1 sentencepiece>=0.2 peft>=0.13 ...
"""#
    /// tests/business/media/Fixtures/install-video-failed.err
    static let installVideoFailedErr = #"""
apogee train: pip install for the 'video' set failed (exit code 1):
ERROR: Could not install packages: this sandbox's pip is a stub with no network
"""#
    /// tests/business/media/Fixtures/generate-refused-runtime-m29.jsonl
    static let generateRefusedM29 = #"""
{"protocol_version":1,"schema":"2026-10-26","type":"job","verb":"video generate"}
{"message":"torch and diffusers are not installed in Apogee's Python environment -- apogee train setup --with video","type":"error"}
"""#
    /// tests/business/media/Fixtures/generate-refused-runtime-m29.err
    static let generateRefusedM29Err = #"""
apogee video generate: torch and diffusers are not installed in Apogee's Python environment -- apogee train setup --with video
"""#
}
#endif
