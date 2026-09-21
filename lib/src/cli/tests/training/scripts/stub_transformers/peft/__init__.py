import os


class TaskType:
    CAUSAL_LM = "CAUSAL_LM"


class LoraConfig:
    def __init__(self, **kwargs):
        self.kwargs = kwargs


def get_peft_model(model, config):
    path = os.environ.get("STUB_RECORD")
    if path:
        with open(path, "a", encoding="utf-8") as f:
            f.write(f"lora: r={config.kwargs.get('r')} alpha={config.kwargs.get('lora_alpha')}\n")
    return model


class PeftModel:
    @staticmethod
    def from_pretrained(base, adapter_path, local_files_only=False):
        path = os.environ.get("STUB_RECORD")
        if path:
            with open(path, "a", encoding="utf-8") as f:
                f.write(f"adapter: {adapter_path}\n")
        base.adapter = adapter_path
        return base


def merge_and_unload(self):
    return self


from transformers import _Model  # noqa: E402

_Model.merge_and_unload = merge_and_unload
