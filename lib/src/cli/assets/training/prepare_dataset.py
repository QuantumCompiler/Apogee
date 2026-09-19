#!/usr/bin/env python3
"""Dataset preparation driver for Apogee.

Converts a local data file (JSONL / JSON / CSV / Parquet) -- or a downloaded
dataset directory holding such files -- into trainer-ready JSONL. Apogee never
reaches the Hugging Face Hub from here: sources are local files only.

JSON, JSONL and CSV are read with the standard library, so the common path
needs nothing installed. Parquet needs the `datasets` library
(`apogee train setup --with prepare`), and it is imported only for Parquet.

Output formats:
  Default (chat):  {"messages": [{"role": "user", ...}, {"role": "assistant", ...}]}
  --flat:          {"prompt": "...", "completion": "..."}
  --as-eval:       {"prompt": "...", "expected": "..."}  (an eval suite)

Emits JSONL progress lines to stdout -- the protocol every Apogee training
script speaks:
  {"message": "..."}
  {"rows_written": N, "rows_skipped": N, "out": "path"}
  {"error": "..."}   <- fatal; a non-zero exit follows
"""

import sys
import json
import argparse
import os
import csv

# Environment guards, BEFORE any ML import. Skipping them causes silent
# SIGABRT crashes that mask the real error (Ommi's recorded lesson).
#
# Must be set before `datasets` imports its Rust tokenizer extension, which
# spawns fork-based workers that can deadlock on macOS.
os.environ.setdefault("TOKENIZERS_PARALLELISM", "false")

# Apogee never reaches the Hugging Face Hub from a script -- force the
# libraries offline so they can only ever read the local file named here.
os.environ["HF_HUB_OFFLINE"] = "1"
os.environ["HF_DATASETS_OFFLINE"] = "1"
os.environ["HF_HUB_DISABLE_TELEMETRY"] = "1"


def emit(obj):
    print(json.dumps(obj), flush=True)


def fail(message):
    emit({"error": message})
    sys.exit(1)


# -- format detection ---------------------------------------------------------

def detect_format(columns):
    """Infer a format preset from column names. Returns the preset or None."""
    col_set = set(columns)
    if "instruction" in col_set and "output" in col_set:
        return "alpaca"
    if "conversations" in col_set:
        return "sharegpt"
    if "role" in col_set and "text" in col_set:
        return "oasst"
    if "prompt" in col_set and "completion" in col_set:
        return "prompt-completion"
    if "messages" in col_set:
        return "chatml"
    return None


# -- row converters ------------------------------------------------------------

def _text(value):
    """A cell as text: None becomes empty, everything else is stringified."""
    if value is None:
        return ""
    if isinstance(value, str):
        return value
    return json.dumps(value)


def make_messages_alpaca(row, col_map):
    """Alpaca: instruction[+input] -> user, output -> assistant (chat format)."""
    instruction = _text(row.get(col_map.get("instruction", "instruction"), ""))
    inp = _text(row.get(col_map.get("input", "input"), ""))
    output = _text(row.get(col_map.get("output", "output")) or
                   row.get(col_map.get("completion", "completion"), ""))
    if not instruction or not output:
        return None
    content = "%s\n\n%s" % (instruction, inp) if inp else instruction
    return {"messages": [
        {"role": "user", "content": content},
        {"role": "assistant", "content": output},
    ]}


def make_flat_alpaca(row, col_map):
    """Alpaca: instruction[+input] -> prompt, output -> completion (flat)."""
    result = make_messages_alpaca(row, col_map)
    if result is None:
        return None
    return {"prompt": result["messages"][0]["content"],
            "completion": result["messages"][1]["content"]}


def _conversation(value):
    """A conversations cell as a list -- CSV carries it as a JSON string."""
    if isinstance(value, str):
        value = value.strip()
        if not value.startswith("["):
            return None
        try:
            value = json.loads(value)
        except ValueError:
            return None
    return value if isinstance(value, list) else None


def make_messages_sharegpt(row, col_map):
    """ShareGPT/ChatML: a conversations list -> messages (chat format).

    Accepts both ShareGPT ("from"/"value") and ChatML ("role"/"content")
    item shapes. Role mapping: human->user, gpt->assistant.
    """
    conv_col = col_map.get("conversations", "conversations")
    conv = _conversation(row.get(conv_col))
    if conv is None:
        conv = _conversation(row.get(col_map.get("messages", "messages")))
    if not conv:
        return None
    role_map = {
        "human": "user", "gpt": "assistant", "system": "system",
        "user": "user", "assistant": "assistant",
    }
    messages = []
    for item in conv:
        if not isinstance(item, dict):
            continue
        role = item.get("from") or item.get("role", "")
        content = item.get("value") or item.get("content", "")
        role = role_map.get(role, role)
        if role and content:
            messages.append({"role": role, "content": _text(content)})
    return {"messages": messages} if messages else None


def make_flat_sharegpt(row, col_map):
    """ShareGPT/ChatML: the last user->assistant pair as prompt/completion."""
    result = make_messages_sharegpt(row, col_map)
    if result is None:
        return None
    user_content = ""
    assistant_content = ""
    for m in result["messages"]:
        if m["role"] == "user":
            user_content = m["content"]
        elif m["role"] == "assistant" and user_content:
            assistant_content = m["content"]
    if not user_content or not assistant_content:
        return None
    return {"prompt": user_content, "completion": assistant_content}


def make_messages_prompt_completion(row, col_map):
    """Passthrough: prompt + completion columns, wrapped in chat format."""
    prompt = _text(row.get(col_map.get("prompt", "prompt"), ""))
    completion = _text(row.get(col_map.get("completion", "completion"), ""))
    if not prompt or not completion:
        return None
    return {"messages": [
        {"role": "user", "content": prompt},
        {"role": "assistant", "content": completion},
    ]}


def make_flat_prompt_completion(row, col_map):
    """Passthrough: prompt + completion columns (flat format)."""
    prompt = _text(row.get(col_map.get("prompt", "prompt"), ""))
    completion = _text(row.get(col_map.get("completion", "completion"), ""))
    if not prompt or not completion:
        return None
    return {"prompt": prompt, "completion": completion}


def to_eval(result):
    """A chat or flat result as an eval item {"prompt", "expected"}."""
    if result is None:
        return None
    if "messages" in result:
        msgs = result["messages"]
        prompt = next((m["content"] for m in msgs if m["role"] == "user"), "")
        expected = next((m["content"] for m in msgs if m["role"] == "assistant"), "")
        if not prompt or not expected:
            return None
        return {"prompt": prompt, "expected": expected}
    if "prompt" in result and "completion" in result:
        return {"prompt": result["prompt"], "expected": result["completion"]}
    return None


# -- loading -------------------------------------------------------------------

def _rows_from_jsonl(path):
    rows = []
    with open(path, "r", encoding="utf-8") as handle:
        for number, line in enumerate(handle, 1):
            line = line.strip()
            if not line:
                continue
            try:
                row = json.loads(line)
            except ValueError as exc:
                fail("%s line %d is not JSON: %s" % (path, number, exc))
            if isinstance(row, dict):
                rows.append(row)
    return rows


def _rows_from_json(path, split):
    with open(path, "r", encoding="utf-8") as handle:
        try:
            data = json.load(handle)
        except ValueError as exc:
            fail("%s is not JSON: %s" % (path, exc))
    if isinstance(data, dict):
        # {"train": [...], "test": [...]} picks the split; {"data": [...]}
        # and a columnar {"col": [...], ...} shape are both accepted. Lists
        # of objects are splits; lists of scalars are columns.
        lists = {k: v for k, v in data.items() if isinstance(v, list)}
        rows_shaped = any(any(isinstance(item, dict) for item in v) for v in lists.values())
        if split in lists and (rows_shaped or not lists[split]):
            data = lists[split]
        elif "data" in lists and not rows_shaped:
            data = lists["data"]
        elif "data" in lists and any(isinstance(item, dict) for item in lists["data"]):
            data = lists["data"]
        elif lists and len(lists) == len(data) and not rows_shaped:
            columns = list(lists.keys())
            length = min(len(v) for v in lists.values())
            data = [{c: lists[c][i] for c in columns} for i in range(length)]
        else:
            fail("split %r not found in %s. Available: %s" % (split, path, sorted(lists.keys())))
    if not isinstance(data, list):
        fail("%s must hold a JSON array of objects" % path)
    return [row for row in data if isinstance(row, dict)]


def _rows_from_csv(path):
    with open(path, "r", encoding="utf-8", newline="") as handle:
        return [dict(row) for row in csv.DictReader(handle)]


def _rows_from_parquet(files, split):
    try:
        from datasets import load_dataset
    except ImportError:
        fail("Parquet needs the 'datasets' library. Install it with: "
             "apogee train setup --with prepare")
    try:
        ds = load_dataset("parquet", data_files=files, split=split)
    except Exception as exc:  # the library raises its own family of errors
        fail("Failed to load %s: %s" % (", ".join(files), exc))
    return [dict(row) for row in ds]


def load_source(source, split):
    """Rows from a local file, or from every data file in a directory."""
    if os.path.isdir(source):
        for ext in ("parquet", "jsonl", "json", "csv"):
            files = sorted(
                os.path.join(source, f)
                for f in os.listdir(source)
                if f.endswith("." + ext)
            )
            if files:
                emit({"message": "Reading %d %s file(s) from %s" % (len(files), ext, source)})
                if ext == "parquet":
                    return _rows_from_parquet(files, split)
                rows = []
                for f in files:
                    rows.extend(_load_file(f, ext, split))
                return rows
        fail("No supported data files found in %r. Expected Parquet, JSON, JSONL "
             "or CSV files." % source)

    if os.path.isfile(source):
        ext = source.rsplit(".", 1)[-1].lower() if "." in source else ""
        if ext not in ("jsonl", "json", "parquet", "csv"):
            fail("Unsupported file extension %r. Supported: jsonl, json, parquet, csv" % ext)
        return _load_file(source, ext, split)

    fail("Source %r is not a file or directory" % source)


def _load_file(path, ext, split):
    if ext == "jsonl":
        return _rows_from_jsonl(path)
    if ext == "json":
        return _rows_from_json(path, split)
    if ext == "csv":
        return _rows_from_csv(path)
    return _rows_from_parquet([path], split)


# -- the row pipeline ----------------------------------------------------------

def process_rows(rows, fmt, col_map, flat, as_eval):
    """Yield output objects for every row, applying the format and col_map.

    OASST is handled specially: consecutive prompter->assistant rows are
    paired. Every other format converts each row independently.
    """
    if fmt == "oasst":
        role_col = col_map.get("role", "role")
        text_col = col_map.get("text", "text")
        role_map = {"prompter": "user", "assistant": "assistant", "user": "user"}
        pending_user = None
        for row in rows:
            role = role_map.get(str(row.get(role_col, "")), "")
            text = _text(row.get(text_col, ""))
            if not role or not text:
                continue
            if role == "user":
                pending_user = text
            elif role == "assistant" and pending_user is not None:
                if flat:
                    result = {"prompt": pending_user, "completion": text}
                else:
                    result = {"messages": [
                        {"role": "user", "content": pending_user},
                        {"role": "assistant", "content": text},
                    ]}
                pending_user = None
                if as_eval:
                    result = to_eval(result)
                if result:
                    yield result
        return

    if flat:
        makers = {
            "alpaca": make_flat_alpaca,
            "sharegpt": make_flat_sharegpt,
            "chatml": make_flat_sharegpt,
            "prompt-completion": make_flat_prompt_completion,
        }
    else:
        makers = {
            "alpaca": make_messages_alpaca,
            "sharegpt": make_messages_sharegpt,
            "chatml": make_messages_sharegpt,
            "prompt-completion": make_messages_prompt_completion,
        }

    make_fn = makers.get(fmt)
    if make_fn is None:
        fail("Unsupported format %r" % fmt)

    for row in rows:
        result = make_fn(row, col_map)
        if as_eval:
            result = to_eval(result)
        if result is not None:
            yield result


# -- main ----------------------------------------------------------------------

def main():
    parser = argparse.ArgumentParser(description="Dataset preparation driver for Apogee")
    parser.add_argument("--source", required=True,
                        help="A local file (JSONL/JSON/CSV/Parquet) or a directory of them")
    parser.add_argument("--out", required=True, help="Output JSONL file path")
    parser.add_argument("--format", default="", dest="fmt",
                        choices=["alpaca", "sharegpt", "chatml", "oasst",
                                 "prompt-completion", ""],
                        help="Format preset (auto-detected when omitted)")
    parser.add_argument("--map", default="", dest="col_map_str",
                        help="Column renames: dest=src[,dest2=src2] "
                             "(e.g. prompt=question,completion=answer)")
    parser.add_argument("--split", default="train",
                        help="Dataset split to use (default: train)")
    parser.add_argument("--as-eval", action="store_true",
                        help="Emit the eval suite shape {prompt, expected}")
    parser.add_argument("--flat", action="store_true",
                        help="Emit {prompt, completion} instead of chat {messages}")
    args = parser.parse_args()

    col_map = {}
    if args.col_map_str:
        for pair in args.col_map_str.split(","):
            pair = pair.strip()
            if not pair:
                continue
            if "=" not in pair:
                fail("Invalid --map entry %r. Expected format: dest=src_column" % pair)
            dest, src = pair.split("=", 1)
            col_map[dest.strip()] = src.strip()

    emit({"message": "Loading dataset from %s (split: %r)..." % (args.source, args.split)})
    rows = load_source(args.source, args.split)
    n_rows = len(rows)
    emit({"message": "Loaded %d rows" % n_rows})

    columns = []
    for row in rows[:100]:
        for key in row.keys():
            if key not in columns:
                columns.append(key)

    fmt = args.fmt
    if not fmt:
        fmt = detect_format(columns)
        if fmt:
            emit({"message": "Auto-detected format: %r" % fmt})
        else:
            col_list = ", ".join(repr(c) for c in columns)
            example = (
                "--map prompt=%s,completion=%s" % (columns[0], columns[1])
                if len(columns) >= 2
                else "--map prompt=<col>,completion=<col>"
            )
            fail("Cannot determine dataset format. Detected columns: %s\n"
                 "Specify a preset with --format or map columns with %s" % (col_list, example))

    out_dir = os.path.dirname(args.out)
    if out_dir:
        os.makedirs(out_dir, exist_ok=True)

    rows_written = 0
    emit({"message": "Converting %d rows with format %r..." % (n_rows, fmt)})
    with open(args.out, "w", encoding="utf-8") as handle:
        for row in process_rows(rows, fmt, col_map, args.flat, args.as_eval):
            handle.write(json.dumps(row) + "\n")
            rows_written += 1
    rows_skipped = max(n_rows - rows_written, 0)

    emit({"message": "Wrote %d rows (%d skipped)" % (rows_written, rows_skipped)})
    emit({"rows_written": rows_written, "rows_skipped": rows_skipped, "out": args.out})


if __name__ == "__main__":
    main()
