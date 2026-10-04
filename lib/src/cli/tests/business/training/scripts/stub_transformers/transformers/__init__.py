"""A stub `transformers` for the protocol tests: a tokenizer that counts
words, a model that saves a marker, a Trainer that logs `max_steps` fake
losses through the callbacks. train_peft.py is run against this on bare
python3 (APOGEE_TRAINING_STUB_MODULES=1 lifts the Apple-Silicon guard,
since no real bitsandbytes is on the path) to prove its emitter."""

import json
import os


def _record(kind, text):
    path = os.environ.get("STUB_RECORD")
    if path:
        with open(path, "a", encoding="utf-8") as f:
            f.write(f"{kind}: {text}\n")


class _Tokenizer:
    pad_token = None
    eos_token = "<eos>"
    eos_token_id = 0
    pad_token_id = 0
    chat_template = "{{ messages }}" if os.environ.get("STUB_CHAT_TEMPLATE") else None

    def __call__(self, text, truncation=False, max_length=None, return_tensors=None):
        ids = [len(word) for word in text.split()]
        return {"input_ids": ids}

    def apply_chat_template(self, messages, add_generation_prompt=False, tokenize=False):
        text = " ".join(f"<{m['role']}> {m['content']}" for m in messages)
        return text + (" <assistant>" if add_generation_prompt else "")

    def decode(self, ids, skip_special_tokens=True):
        return "stub decoded " + " ".join(str(i) for i in ids)

    def save_pretrained(self, path):
        os.makedirs(path, exist_ok=True)
        with open(os.path.join(path, "tokenizer.json"), "w", encoding="utf-8") as f:
            f.write("{}")


class AutoTokenizer:
    @staticmethod
    def from_pretrained(path, **kwargs):
        _record("tokenizer", path)
        return _Tokenizer()


class _Model:
    device = "cpu"

    def enable_input_require_grads(self):
        _record("model", "input_require_grads")

    def gradient_checkpointing_enable(self):
        _record("model", "gradient_checkpointing")

    def eval(self):
        return self

    def generate(self, input_ids, max_new_tokens=0, do_sample=False, pad_token_id=None):
        return [list(input_ids[0]) + [7, 8, 9]]

    def save_pretrained(self, path, safe_serialization=False):
        os.makedirs(path, exist_ok=True)
        with open(os.path.join(path, "model.safetensors"), "w", encoding="utf-8") as f:
            f.write("stub")


class AutoModelForCausalLM:
    @staticmethod
    def from_pretrained(path, **kwargs):
        _record("model", f"{path} quantized={'quantization_config' in kwargs}")
        return _Model()


class BitsAndBytesConfig:
    def __init__(self, **kwargs):
        self.kwargs = kwargs


class TrainingArguments:
    def __init__(self, **kwargs):
        self.kwargs = kwargs
        self.max_steps = kwargs.get("max_steps", 0)


class TrainerCallback:
    pass


class _State:
    global_step = 0


class Trainer:
    def __init__(self, model, args, train_dataset, data_collator=None, callbacks=None):
        self.model = model
        self.args = args
        self.dataset = train_dataset
        self.collator = data_collator
        self.callbacks = callbacks or []

    def train(self):
        # Exercise the collator once, as the real Trainer would.
        batch = self.collator([self.dataset[0]])
        _record("batch", json.dumps({k: list(v) for k, v in batch.items()}))
        state = _State()
        for step in range(1, self.args.max_steps + 1):
            state.global_step = step
            logs = {"loss": 3.0 - step * 0.5, "learning_rate": 2e-4, "train_steps_per_second": 1.5}
            for callback in self.callbacks:
                callback.on_log(self.args, state, None, logs=logs)
