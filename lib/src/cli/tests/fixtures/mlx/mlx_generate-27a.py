#!/usr/bin/env python3
"""MLX inference driver for Apogee's `mlx` backend (Apple silicon).

One process per chat session. It loads ONE model directory (a Hugging Face
layout -- config.json, the tokenizer files, SafeTensors weights) through
`mlx_lm`, holds the model and its attention cache in memory across turns, and
answers requests until its stdin closes, then exits. It never listens on a
port, never reads Apogee's config, and never touches the model store: the
backend tells it everything it needs, one request at a time.

Runs under the Python environment Apogee owns, never the system Python.
Requires `mlx-lm` there:  apogee train setup --with mlx

The protocol, one JSON object per line.

In, on stdin:
  {"type": "generate", "id": N, "messages": [...], "tools": [...],
   "sampling": {"temperature": F, "top_p": F, "top_k": N, "min_p": F,
                "repetition_penalty": F, "presence_penalty": F, "seed": N|null},
   "max_tokens": N, "stop": ["..."], "thinking": true|false,
   "session": true|false}
      `messages` and `tools` are the chat-template shapes Hugging Face
      tokenizers take. `session: false` is a side request: it is answered on
      a cache of its own and leaves the conversation's untouched.
  {"type": "cancel", "id": N}
      Ends request N's generation; its `done` says "cancelled".
  Closing stdin ends the driver: a generation in flight stops (its `done`
  says "cancelled"), nothing queued behind it starts, and it exits 0.

Out, on stdout:
  {"type": "ready", "protocol": 1, "model_type": "...", "chat_template": B,
   "tool_parser": "..."|null, "thinking": B, "mlx_lm": "..."}
      Once, after the model loads. `tool_parser` names mlx_lm's parser for
      the model's call format, or null when it has none.
  {"type": "text", "id": N, "text": "..."}        answer text, as generated
  {"type": "reasoning", "id": N, "text": "..."}   the model's reasoning
  {"type": "tool_call", "id": N, "name": "...", "arguments": {...}}
  {"type": "done", "id": N, "finish": "stop"|"length"|"tool_calls"|"cancelled",
   "prompt_tokens": N, "cached_tokens": N, "completion_tokens": N}
  {"type": "error", "id": N|null, "kind": "missing_dependency"|"load"|
   "request"|"protocol", "message": "..."}
      A missing dependency or a failed load is fatal: the driver exits
      non-zero after saying so. A bad request is not.

Reasoning and tool calls are read in the model's OWN format: the markers the
tokenizer declares (`mlx_lm` knows each family's), parsed by `mlx_lm`'s own
parser for that format. A family `mlx_lm` has no parser for streams as text,
and the backend's per-family filters read it -- with one exception, the reply
that IS a JSON call (Llama 3's documented tool format), read here because
only here are the offered tools known.

Everything a library prints goes to stderr, which Apogee captures: the
protocol's stdout is a duplicate of the original descriptor, and descriptor 1
itself is pointed at stderr before anything is imported.
"""

import copy
import json
import os
import select
import signal
import sys

# ── Protective environment, before any ML import ─────────────────────────────
# KMP_DUPLICATE_LIB_OK: two bundled libomp copies in one process abort without it.
# TOKENIZERS_PARALLELISM: forked tokenizer workers deadlock on macOS.
os.environ.setdefault("KMP_DUPLICATE_LIB_OK", "TRUE")
os.environ.setdefault("TOKENIZERS_PARALLELISM", "false")
# The model is a local directory. These make anything else fail loudly rather
# than download quietly.
os.environ["HF_HUB_OFFLINE"] = "1"
os.environ["TRANSFORMERS_OFFLINE"] = "1"
os.environ["HF_HUB_DISABLE_TELEMETRY"] = "1"

PROTOCOL = 1

# Ctrl-C at a terminal reaches the whole foreground process group, this child
# with it. The backend cancels a turn over the protocol and keeps the model
# loaded; a driver killed by the same keystroke would cost a reload.
signal.signal(signal.SIGINT, signal.SIG_IGN)

# The protocol channel, then every stray write to descriptor 1 sent to stderr:
# a library's print() must never land between two protocol lines.
_OUT = os.fdopen(os.dup(1), "w", encoding="utf-8", newline="\n")
os.dup2(2, 1)
sys.stdout = sys.stderr


def emit(obj):
    _OUT.write(json.dumps(obj) + "\n")
    _OUT.flush()


def error(kind, message, rid=None):
    emit({"type": "error", "id": rid, "kind": kind, "message": message})


class Input:
    """Lines from stdin, read from the descriptor itself so a poll between
    two tokens sees exactly what has arrived (a buffered reader would hide a
    cancel inside its buffer)."""

    def __init__(self):
        self.fd = sys.stdin.fileno()
        self.buffer = b""
        self.eof = False
        self.queued = []

    def _fill(self, timeout):
        if self.eof:
            return False
        ready, _, _ = select.select([self.fd], [], [], timeout)
        if not ready:
            return False
        chunk = os.read(self.fd, 65536)
        if not chunk:
            self.eof = True
            return False
        self.buffer += chunk
        return True

    def _take(self):
        lines = []
        while b"\n" in self.buffer:
            line, self.buffer = self.buffer.split(b"\n", 1)
            lines.append(line.decode("utf-8", "replace"))
        return lines

    def next_line(self):
        """The next line, blocking; None once stdin has closed."""
        while True:
            if self.queued:
                return self.queued.pop(0)
            lines = self._take()
            if lines:
                self.queued.extend(lines)
                continue
            if self.eof:
                if self.buffer:
                    rest, self.buffer = self.buffer, b""
                    return rest.decode("utf-8", "replace")
                return None
            self._fill(None)

    def poll(self):
        """Lines that have arrived, without waiting."""
        while self._fill(0):
            pass
        lines = self._take()
        return lines


def parse_json_line(line):
    line = line.strip()
    if not line:
        return None
    try:
        value = json.loads(line)
    except ValueError:
        return {"type": "_invalid", "text": line}
    return value if isinstance(value, dict) else {"type": "_invalid", "text": line}


# ── Reading a reply ──────────────────────────────────────────────────────────

PYTHON_TAG = "<|python_tag|>"


def json_calls(text, tool_names):
    """Llama 3's tool format: the reply IS the call, as
    {"name": ..., "parameters": {...}} (or "arguments"), optionally after
    <|python_tag|>. One call: the format holds one per message, and Llama's
    own template refuses to render a second back ("only supports single
    tool-calls at once"), so -- as llama.cpp's reader does -- the first object
    is the call and whatever follows it is dropped. An object that does not
    name an offered tool with an arguments object is not a call at all."""
    text = text.strip()
    if text.startswith(PYTHON_TAG):
        text = text[len(PYTHON_TAG):].strip()
    try:
        value, _ = json.JSONDecoder().raw_decode(text)
    except ValueError:
        return None
    if isinstance(value, list):
        value = value[0] if value else None
    if not isinstance(value, dict):
        return None
    if isinstance(value.get("function"), dict):
        value = value["function"]
    name = value.get("name")
    arguments = value.get("parameters", value.get("arguments"))
    if isinstance(arguments, str):
        try:
            arguments = json.loads(arguments)
        except ValueError:
            return None
    if not isinstance(name, str) or name not in tool_names or not isinstance(arguments, dict):
        return None
    return [{"name": name, "arguments": arguments}]


class Reader:
    """Splits generated text into answer, reasoning and tool calls by the
    markers the model's format declares, holding back any tail that could
    still become a marker. Stop strings end the reply."""

    def __init__(self, rid, think, tool, parser, tools, json_tools, stops, reasoning):
        self.rid = rid
        self.think = think          # (start, end) or None
        self.tool = tool            # (start, end) or None; end may be None
        self.parser = parser
        self.tools = tools
        self.tool_names = {t.get("function", {}).get("name") for t in tools or []}
        self.stops = [s for s in stops if s]
        self.state = "reasoning" if reasoning else "normal"
        self.pending = ""
        self.tool_text = ""
        self.calls = []
        self.stopped = False
        # Llama 3's JSON reply: decided on the reply's first visible text.
        self.lead = bool(json_tools) and not reasoning
        self.lead_text = ""
        self.holding_json = False

    def _out(self, kind, text):
        if text:
            emit({"type": kind, "id": self.rid, "text": text})

    def _markers(self):
        markers = []
        if self.state == "normal":
            if self.think:
                markers.append((self.think[0], "reasoning"))
            if self.tool:
                markers.append((self.tool[0], "tool"))
        elif self.state == "reasoning":
            if self.think:
                markers.append((self.think[1], "normal"))
            if self.tool:
                markers.append((self.tool[0], "tool"))
        elif self.state == "tool":
            if self.tool and self.tool[1]:
                markers.append((self.tool[1], "normal"))
            return markers
        for stop in self.stops:
            markers.append((stop, None))
        return markers

    def _finish_tool(self, closed):
        text = self.tool_text
        self.tool_text = ""
        parsed = None
        if self.parser is not None:
            try:
                parsed = self.parser(text, self.tools)
            except Exception:  # noqa: BLE001 -- any parser failure is "not a call"
                parsed = None
        items = parsed if isinstance(parsed, list) else ([parsed] if parsed else [])
        calls = []
        for item in items:
            if isinstance(item, dict) and isinstance(item.get("name"), str):
                arguments = item.get("arguments", {})
                if isinstance(arguments, str):
                    try:
                        arguments = json.loads(arguments)
                    except ValueError:
                        arguments = None
                if isinstance(arguments, dict):
                    calls.append({"name": item["name"], "arguments": arguments})
                    continue
            calls = None
            break
        if calls:
            for call in calls:
                self.calls.append(call)
                emit({"type": "tool_call", "id": self.rid, "name": call["name"],
                      "arguments": call["arguments"]})
            return
        # The safety net: a span that opened like a call and is not one goes
        # back out as text, never silently dropped.
        raw = self.tool[0] + text + (self.tool[1] if closed and self.tool[1] else "")
        self._out("text", raw)

    def _route(self, text):
        if not text:
            return
        if self.state == "normal":
            self._out("text", text)
        elif self.state == "reasoning":
            self._out("reasoning", text)
        else:
            self.tool_text += text

    def feed(self, piece):
        """Consumes generated text; True once a stop string ended the reply."""
        if self.stopped or not piece:
            return self.stopped
        if self.holding_json:
            self.lead_text += piece
            return False
        if self.lead:
            self.lead_text += piece
            stripped = self.lead_text.lstrip()
            if not stripped or PYTHON_TAG.startswith(stripped):
                return False
            if stripped.startswith(PYTHON_TAG):
                stripped = stripped[len(PYTHON_TAG):].lstrip()
                if not stripped:
                    return False
            self.lead = False
            if stripped.startswith("{") or stripped.startswith("["):
                self.holding_json = True
                return False
            piece, self.lead_text = self.lead_text, ""
        self.pending += piece
        while True:
            markers = self._markers()
            best = None
            for marker, target in markers:
                at = self.pending.find(marker)
                if at >= 0 and (best is None or at < best[0]):
                    best = (at, marker, target)
            if best is None:
                break
            at, marker, target = best
            self._route(self.pending[:at])
            self.pending = self.pending[at + len(marker):]
            if target is None:
                self.stopped = True
                self.pending = ""
                return True
            if self.state == "tool" and target == "normal":
                self.state = "normal"
                self._finish_tool(closed=True)
            else:
                self.state = target
        # Hold the longest tail that could still begin a marker.
        hold = 0
        for marker, _ in self._markers():
            for size in range(min(len(marker) - 1, len(self.pending)), 0, -1):
                if self.pending.endswith(marker[:size]):
                    hold = max(hold, size)
                    break
        if hold:
            self._route(self.pending[:-hold])
            self.pending = self.pending[-hold:]
        else:
            self._route(self.pending)
            self.pending = ""
        return False

    def finish(self):
        """Ends the reply: what was held is resolved."""
        if self.holding_json or self.lead:
            text, self.lead_text = self.lead_text, ""
            self.holding_json = False
            self.lead = False
            calls = json_calls(text, self.tool_names) if text.strip() else None
            if calls:
                for call in calls:
                    self.calls.append(call)
                    emit({"type": "tool_call", "id": self.rid, "name": call["name"],
                          "arguments": call["arguments"]})
                return
            self.feed(text)
        if self.state == "tool":
            self.tool_text += self.pending
            self.pending = ""
            self._finish_tool(closed=False)
            return
        self._route(self.pending)
        self.pending = ""


# ── The model ────────────────────────────────────────────────────────────────


class Driver:
    def __init__(self, mx, model, tokenizer, api, model_type):
        self.mx = mx
        self.model = model
        self.tokenizer = tokenizer
        self.api = api
        self.model_type = model_type
        self.cache = None
        self.cached = []
        # The cache as it stood where the last prompt's history ended (before
        # its generation prompt), kept only when the cache cannot be cut back
        # -- a sliding window past its size, a recurrent state: the next
        # prompt almost always extends that history, and this is what lets it
        # read only what is new.
        self.checkpoint = None
        self.input = Input()

    def ready(self, version):
        tokenizer = self.tokenizer
        tool_parser = None
        if getattr(tokenizer, "has_tool_calling", False):
            parser = getattr(tokenizer, "tool_parser", None)
            module = getattr(parser, "__module__", "") or ""
            tool_parser = module.rsplit(".", 1)[-1] or "unnamed"
        emit({
            "type": "ready",
            "protocol": PROTOCOL,
            "model_type": self.model_type,
            "chat_template": self.has_template(),
            "tool_parser": tool_parser,
            "thinking": bool(getattr(tokenizer, "has_thinking", False)),
            "mlx_lm": version,
        })

    def has_template(self):
        return bool(getattr(self.tokenizer, "has_chat_template", False))

    def encode(self, text):
        bos = getattr(self.tokenizer, "bos_token", None)
        add_special = bos is None or not text.startswith(bos)
        return list(self.tokenizer.encode(text, add_special_tokens=add_special))

    def render(self, request):
        """The prompt's text and tokens, and where its history ends: the
        boundary before the generation prompt, which a template may write
        differently once the turn it opens is in the history (Gemma 4 opens a
        thinking channel there and drops it after) -- so a checkpoint is
        taken there, the last point the next prompt is sure to share."""
        messages = request.get("messages") or []
        tools = request.get("tools") or None
        tokenizer = self.tokenizer
        if self.has_template():
            kwargs = {"add_generation_prompt": True, "tokenize": False}
            if tools:
                kwargs["tools"] = tools
            if "thinking" in request and request["thinking"] is not None:
                kwargs["enable_thinking"] = bool(request["thinking"])
            text = tokenizer.apply_chat_template(messages, **kwargs)
            prompt = self.encode(text)
            boundary = len(prompt) - 1
            try:
                kwargs["add_generation_prompt"] = False
                history = self.encode(tokenizer.apply_chat_template(messages, **kwargs))
                if 0 < len(history) < len(prompt) and prompt[:len(history)] == history:
                    boundary = len(history)
            except Exception:  # noqa: BLE001 -- no boundary is the cautious answer
                pass
            return text, prompt, boundary
        else:
            # A base model: no format of its own, so a plain transcript it can
            # continue, ended where it starts writing the next user turn.
            lines = []
            for message in messages:
                role = message.get("role", "user")
                label = {"system": "System", "assistant": "Assistant"}.get(role, "User")
                lines.append(f"{label}: {message.get('content') or ''}")
            text = "\n\n".join(lines) + "\n\nAssistant:"
        prompt = self.encode(text)
        return text, prompt, len(prompt) - 1

    def prepare(self, prompt, session):
        """The cache to generate on and the prompt tokens it still needs."""
        make, can_trim, trim = self.api["cache"]
        if not session:
            return make(self.model), prompt, 0
        if self.cache is None:
            self.cache = make(self.model)
            self.cached = []
        shared = 0
        limit = min(len(self.cached), len(prompt))
        while shared < limit and self.cached[shared] == prompt[shared]:
            shared += 1
        # At least one token is always fed: the next token's logits come from it.
        shared = min(shared, len(prompt) - 1)
        excess = len(self.cached) - shared
        if excess > 0:
            trimmed = trim(self.cache, excess) if can_trim(self.cache) else 0
            if trimmed != excess:
                self.cache, shared = self.restore(prompt)
        self.cached = prompt[:shared]
        return self.cache, prompt[shared:], shared

    def restore(self, prompt):
        """A cache that cannot be cut back: the checkpoint, when this prompt
        extends what it holds, else a fresh one."""
        make = self.api["cache"][0]
        if self.checkpoint is not None:
            tokens, saved = self.checkpoint
            if len(tokens) < len(prompt) and prompt[:len(tokens)] == tokens:
                return copy.deepcopy(saved), len(tokens)
        self.checkpoint = None
        return make(self.model), 0

    def feed(self, cache, tokens):
        """Reads `tokens` into `cache`, in chunks, as mlx_lm's own step does."""
        for start in range(0, len(tokens), 2048):
            self.model(self.mx.array(tokens[start:start + 2048])[None], cache=cache)
            self.mx.eval([c.state for c in cache])

    def prefill(self, cache, prompt, shared, boundary):
        """Reads the prompt from `shared` to its last token -- the step reads
        that one, so its logits are the first sample's -- checkpointing the
        cache at `boundary` when it can no longer be cut back there. Returns
        the tokens left for the step."""
        can_trim = self.api["cache"][1]
        end = len(prompt) - 1
        cut = boundary if shared < boundary <= end else end
        self.feed(cache, prompt[shared:cut])
        if cut > shared and not can_trim(cache):
            self.checkpoint = (prompt[:cut], copy.deepcopy(cache))
        self.feed(cache, prompt[cut:end])
        return prompt[end:]

    def cancelled(self, rid):
        for line in self.input.poll():
            message = parse_json_line(line)
            if message is None:
                continue
            if message.get("type") == "cancel" and message.get("id") in (None, rid):
                return True
            if message.get("type") != "cancel":
                self.input.queued.append(line)
        return self.input.eof

    def generate(self, request):
        rid = request.get("id")
        session = request.get("session", True) is not False
        tokenizer = self.tokenizer
        text, prompt, boundary = self.render(request)
        if not prompt:
            error("request", "the rendered prompt is empty", rid)
            return
        cache, suffix, shared = self.prepare(prompt, session)

        sampling = request.get("sampling") or {}
        make_sampler, make_logits_processors = self.api["sampling"]
        sampler = make_sampler(
            temp=float(sampling.get("temperature", 0.0) or 0.0),
            top_p=float(sampling.get("top_p", 1.0) if sampling.get("top_p") is not None else 1.0),
            min_p=float(sampling.get("min_p", 0.0) or 0.0),
            top_k=int(sampling.get("top_k", 0) or 0),
        )
        repetition = sampling.get("repetition_penalty")
        presence = sampling.get("presence_penalty")
        processors = make_logits_processors(
            repetition_penalty=repetition if repetition not in (None, 1.0, 1) else None,
            presence_penalty=presence if presence not in (None, 0.0, 0) else None,
        )
        if sampling.get("seed") is not None:
            self.mx.random.seed(int(sampling["seed"]))

        tools = request.get("tools") or []
        think = None
        if getattr(tokenizer, "has_thinking", False):
            think = (tokenizer.think_start, tokenizer.think_end)
        tool = None
        parser = None
        if tools and getattr(tokenizer, "has_tool_calling", False):
            tool = (tokenizer.tool_call_start, tokenizer.tool_call_end)
            parser = tokenizer.tool_parser
        reasoning = False
        if think is not None:
            opened = text.rfind(think[0])
            reasoning = opened >= 0 and opened > text.rfind(think[1])
        stops = list(request.get("stop") or [])
        if not self.has_template():
            stops.append("\nUser:")
        reader = Reader(rid, think, tool, parser, tools,
                        bool(tools) and tool is None and self.has_template(),
                        stops, reasoning)

        eos = set(getattr(tokenizer, "eos_token_ids", None) or [])
        detokenizer = tokenizer.detokenizer
        detokenizer.reset()
        max_tokens = int(request.get("max_tokens") or 2048)
        generated = []
        produced = 0
        finish = "length"
        if session:
            suffix = self.prefill(cache, prompt, shared, boundary)
        steps = self.api["generate_step"](
            self.mx.array(suffix), self.model, max_tokens=max_tokens, sampler=sampler,
            logits_processors=processors, prompt_cache=cache)
        try:
            for token, _ in steps:
                token = int(token)
                # The step fed this token into the cache before handing it over.
                generated.append(token)
                if token in eos:
                    finish = "stop"
                    break
                produced += 1
                detokenizer.add_token(token)
                if reader.feed(detokenizer.last_segment):
                    finish = "stop"
                    break
                if self.cancelled(rid):
                    finish = "cancelled"
                    break
        finally:
            close = getattr(steps, "close", None)
            if close is not None:
                close()
        if finish != "cancelled" and not reader.stopped:
            detokenizer.finalize()
            reader.feed(detokenizer.last_segment)
        reader.finish()
        if session:
            self.cached = prompt + generated
        if reader.calls and finish != "cancelled":
            finish = "tool_calls"
        emit({"type": "done", "id": rid, "finish": finish, "prompt_tokens": len(prompt),
              "cached_tokens": shared, "completion_tokens": produced})

    def serve(self):
        while True:
            line = self.input.next_line()
            # Closed stdin ends the driver: nothing queued behind it starts.
            if line is None or self.input.eof:
                return 0
            request = parse_json_line(line)
            if request is None:
                continue
            kind = request.get("type")
            if kind == "generate":
                try:
                    self.generate(request)
                except Exception as e:  # noqa: BLE001 -- reported, and the driver lives on
                    # The cache may hold half a turn: start the next one clean.
                    self.cache = None
                    self.cached = []
                    self.checkpoint = None
                    error("request", f"{type(e).__name__}: {e}", request.get("id"))
            elif kind == "cancel":
                continue  # nothing running: a cancel that arrived late
            else:
                error("protocol", f"unknown request: {line[:200]}", request.get("id"))


def model_type_of(model_dir):
    try:
        with open(os.path.join(model_dir, "config.json"), encoding="utf-8") as f:
            config = json.load(f)
    except (OSError, ValueError):
        return None
    return config.get("model_type") if isinstance(config, dict) else None


def main(argv):
    model_dir = None
    if len(argv) >= 3 and argv[1] == "--model":
        model_dir = argv[2]
    if not model_dir:
        error("protocol", "usage: mlx_generate.py --model <model directory>")
        return 2

    try:
        import mlx.core as mx
        import mlx_lm
        from mlx_lm import load
        from mlx_lm.generate import generate_step
        from mlx_lm.models.cache import can_trim_prompt_cache, make_prompt_cache, trim_prompt_cache
        from mlx_lm.sample_utils import make_logits_processors, make_sampler
    except ImportError as e:
        error("missing_dependency",
              f"mlx-lm is not installed in the Python environment ({e}). "
              "Run: apogee train setup --with mlx")
        return 3

    if not os.path.isfile(os.path.join(model_dir, "config.json")):
        error("load", f"{model_dir} is not a model directory: it has no config.json")
        return 4
    model_type = model_type_of(model_dir)
    try:
        model, tokenizer = load(model_dir)
    except Exception as e:  # noqa: BLE001 -- the reason is the message
        error("load", f"could not load {model_dir}: {type(e).__name__}: {e}")
        return 4

    api = {
        "generate_step": generate_step,
        "sampling": (make_sampler, make_logits_processors),
        "cache": (make_prompt_cache, can_trim_prompt_cache, trim_prompt_cache),
    }
    driver = Driver(mx, model, tokenizer, api, model_type)
    driver.ready(getattr(mlx_lm, "__version__", "unknown"))
    return driver.serve()


if __name__ == "__main__":
    sys.exit(main(sys.argv))
