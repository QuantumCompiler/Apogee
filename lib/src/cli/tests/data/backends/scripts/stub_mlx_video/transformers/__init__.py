"""A stub `transformers` beside the stub `mlx_video` (53l): what mlx-video's
Wan generator asks of it -- `AutoTokenizer.from_pretrained`. Like the real one
under `HF_HUB_OFFLINE=1`, a hub name is refused (nothing is cached here) and
only a directory holding a tokenizer's files opens."""

import os


class _Tokenizer:
    def __init__(self, path, local_files_only):
        self.path = path
        self.local_files_only = local_files_only


class AutoTokenizer:
    @classmethod
    def from_pretrained(cls, name, *_, **options):
        local = bool(options.get("local_files_only"))
        if os.path.isdir(name) and os.path.isfile(os.path.join(name, "tokenizer_config.json")):
            return _Tokenizer(name, local)
        if os.environ.get("HF_HUB_OFFLINE") == "1":
            raise OSError(
                f"We couldn't connect to 'https://huggingface.co' to load the files, and "
                f"couldn't find them in the cached files for {name}"
            )
        raise RuntimeError(f"the stub transformers would have fetched {name} from the hub")
