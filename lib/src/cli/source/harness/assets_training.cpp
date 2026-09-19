#include <array>
#include <string_view>

#include "harness/assets.h"

// GENERATED from lib/src/cli/assets/training/kits/*.yaml and
// assets/training/prepare_dataset.py by the script recorded in MILESTONES.md
// (Milestone Z). The shipped files and these literals are byte-identical, and
// tests/harness/assets_test.cpp fails the build the moment they drift -- the
// same contract the config template and the bundled agents keep. Edit the
// FILES, then regenerate; never edit a literal here.

namespace apogee::harness {
namespace {

constexpr std::string_view k_kit_instruction_following = R"KIT(name: instruction-following
description: Follow explicit instructions precisely — format, length, and constraints.
skill: instruction-following

synth:
  system: |
    You produce supervised fine-tuning examples that teach a small language model
    to follow explicit user instructions exactly. Each example pairs a user prompt
    that contains one or more concrete, checkable constraints (an exact word count,
    a required prefix or suffix, an output format, a forbidden word, a number of
    list items, a casing rule, etc.) with an assistant completion that satisfies
    every constraint precisely and adds nothing extra. The constraints must be
    unambiguous so compliance is objectively verifiable. Cover a wide range of
    instruction types and difficulty.
  seeds:
    - "exact length constraints (e.g. 'in exactly 7 words', 'in under 20 characters')"
    - "required formatting (bullet list of N items, numbered steps, a single sentence)"
    - "casing and punctuation rules (ALL CAPS, no punctuation, title case)"
    - "must-start-with / must-end-with constraints"
    - "forbidden words or required keywords"
    - "answer-only constraints ('reply with only the number', 'one word only')"
    - "structured replies (key: value pairs, a short table, CSV row)"
    - "tone and persona constraints kept short and checkable"
  count: 200
  per_seed: 8
  temperature: 0.9

train:
  iters: 400
  num_layers: 16

eval:
  - prompt: "Reply with only the word DONE in all capital letters. No punctuation."
    expected: "DONE"
  - prompt: "Answer with a single digit only: how many sides does a triangle have?"
    expected: "3"
  - prompt: "Begin your reply with the prefix 'RESULT:' and then state the capital of Japan."
    expected: "RESULT:"
  - prompt: "Reply with exactly one word naming the color of a clear daytime sky."
    expected: "blue"
  - prompt: "List exactly three primary colors as a comma-separated list, lowercase."
    expected: "red"
  - prompt: "Respond with only YES or only NO: is 10 greater than 7?"
    expected: "YES"
  - prompt: "End your response with the exact suffix '-- end'. Say hello first."
    expected: "-- end"
  - prompt: "Output the number 42 and nothing else."
    expected: "42"
)KIT";

constexpr std::string_view k_kit_structured_output = R"KIT(name: structured-output
description: Emit well-formed JSON that matches a requested shape.
skill: structured-output

synth:
  system: |
    You produce supervised fine-tuning examples that teach a small language model
    to return well-formed, minimal JSON. Each example pairs a user prompt that asks
    for information "as JSON" with a named set of fields, and an assistant
    completion that is ONLY a single valid JSON object (or array) with exactly the
    requested fields — no prose, no markdown fences, no trailing commentary. Keys
    must match what the user asked for. Values must be plausible and correctly
    typed (strings quoted, numbers bare, booleans lowercase). Vary the domains
    (people, products, geography, events, configuration) and the field sets.
  seeds:
    - "object with fields: name (string), age (number)"
    - "object describing a city: city, country, population (number)"
    - "object with a boolean flag and a short reason string"
    - "array of 3 objects, each {id, label}"
    - "object with nested object (e.g. address with city and zip)"
    - "object representing a product: name, price (number), in_stock (boolean)"
    - "object with an array field (e.g. tags: [..])"
    - "configuration object with mixed types"
  count: 200
  per_seed: 8
  temperature: 0.8

train:
  iters: 400
  num_layers: 16

eval:
  - prompt: "Return JSON with a single field \"status\" set to the string \"ok\". JSON only."
    expected: "\"status\""
  - prompt: "Give me a JSON object with field \"count\" set to the number 5. JSON only, no markdown."
    expected: "\"count\""
  - prompt: "Return a JSON object describing France with fields \"country\" and \"capital\". JSON only."
    expected: "\"capital\""
  - prompt: "Return JSON with a boolean field \"enabled\" set to true. JSON only."
    expected: "true"
  - prompt: "Return a JSON object with field \"name\" set to \"Ada\". JSON only."
    expected: "\"name\""
  - prompt: "Return a JSON array containing the three strings \"a\", \"b\", \"c\". JSON only."
    expected: "["
  - prompt: "Return JSON with field \"price\" set to the number 19. JSON only."
    expected: "\"price\""
  - prompt: "Return a JSON object with field \"ok\" set to the boolean false. JSON only."
    expected: "false"
)KIT";

constexpr std::string_view k_kit_summarization = R"KIT(name: summarization
description: Condense a passage into a faithful, concise summary.
skill: summarization

synth:
  system: |
    You produce supervised fine-tuning examples that teach a small language model
    to summarize. Each example pairs a user prompt — a short self-contained passage
    (3 to 8 sentences) followed by an instruction to summarize it (e.g. "Summarize
    the passage in one sentence." or "Give a two-sentence summary.") — with an
    assistant completion that is a faithful, concise summary capturing the main
    point and the key named entities, introducing no facts that are not in the
    passage. Vary the domains (science, history, business, technology, everyday
    life) and the requested summary lengths. Include the full passage text inside
    the prompt so the example is self-contained.
  seeds:
    - "a short science/nature passage, summarize in one sentence"
    - "a brief news-style passage about a company or product"
    - "a historical paragraph mentioning a person and a year"
    - "a how-something-works explanation, two-sentence summary"
    - "a passage describing a place or landmark"
    - "a short biography paragraph"
    - "a passage about a recent event with a named organization"
    - "a paragraph with a clear main claim and supporting detail"
  count: 200
  per_seed: 6
  temperature: 0.8

train:
  iters: 400
  num_layers: 16

eval:
  - prompt: "Summarize in one sentence: The Eiffel Tower is a wrought-iron lattice tower in Paris, France. It was completed in 1889 and was the tallest structure in the world until 1930."
    expected: "Eiffel"
  - prompt: "Summarize in one sentence: Photosynthesis is the process by which green plants use sunlight to convert carbon dioxide and water into glucose and oxygen."
    expected: "Photosynthesis"
  - prompt: "Summarize in one sentence: Acme Corp announced a new electric scooter on Tuesday. The company said the scooter has a range of 40 miles on a single charge."
    expected: "Acme"
  - prompt: "Summarize in one sentence: The Great Barrier Reef, located off the coast of Australia, is the world's largest coral reef system and is visible from space."
    expected: "reef"
  - prompt: "Summarize in one sentence: Marie Curie was a physicist and chemist who conducted pioneering research on radioactivity and won two Nobel Prizes."
    expected: "Curie"
  - prompt: "Summarize in one sentence: The Amazon rainforest produces a significant share of the world's oxygen and is home to millions of species of plants and animals."
    expected: "Amazon"
  - prompt: "Summarize in one sentence: A solar eclipse occurs when the Moon passes between the Earth and the Sun, briefly blocking the Sun's light."
    expected: "eclipse"
  - prompt: "Summarize in one sentence: The company Globex reported record quarterly revenue, driven mainly by strong sales of its cloud software products."
    expected: "Globex"
)KIT";

constexpr std::string_view k_kit_reasoning = R"KIT(name: reasoning
description: Solve arithmetic and logic problems with a correct final answer.
skill: reasoning

synth:
  system: |
    You produce supervised fine-tuning examples that teach a small language model
    to reason through arithmetic and simple logic problems and state a correct
    final answer. Each example pairs a user prompt — a self-contained word problem
    or arithmetic/logic question with a single unambiguous answer — with an
    assistant completion that works through the steps briefly and ends with the
    correct final answer clearly stated (e.g. on a final line "Answer: 150").
    The final answer MUST be correct. Vary the problem types: percentages, rates
    and distances, ratios, basic algebra, counting, unit conversions, and short
    deductive-logic puzzles. Keep numbers clean so the answer is exact.
  seeds:
    - "percentage problems (e.g. 'what is X% of Y')"
    - "rate / distance / time word problems"
    - "ratio and proportion problems"
    - "basic single-variable algebra"
    - "counting and combinatorics (small numbers)"
    - "unit conversion problems"
    - "sequence / pattern next-term problems"
    - "short deductive logic puzzles with a definite answer"
  count: 200
  per_seed: 8
  temperature: 0.7

train:
  iters: 500
  num_layers: 16

eval:
  - prompt: "What is 15% of 240?"
    expected: "36"
  - prompt: "If a train travels at 60 mph for 2.5 hours, how far does it go? Give the number of miles."
    expected: "150"
  - prompt: "What is the square root of 144?"
    expected: "12"
  - prompt: "A shirt costs $40 and is discounted by 25%. What is the sale price in dollars?"
    expected: "30"
  - prompt: "If 3x = 21, what is x?"
    expected: "7"
  - prompt: "How many minutes are there in 3 hours?"
    expected: "180"
  - prompt: "What is the next number in the sequence 2, 4, 8, 16, ...?"
    expected: "32"
  - prompt: "A basket has 5 apples and you add 7 more, then remove 3. How many apples are there?"
    expected: "9"
)KIT";

constexpr std::string_view k_prepare_dataset_py = R"PY(#!/usr/bin/env python3
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
)PY";

constexpr std::array<BundledKit, 4> kKits{{
    {"instruction-following", k_kit_instruction_following},
    {"structured-output", k_kit_structured_output},
    {"summarization", k_kit_summarization},
    {"reasoning", k_kit_reasoning},
}};

constexpr std::array<BundledScript, 1> kScripts{{
    {"prepare_dataset.py", k_prepare_dataset_py},
}};

}  // namespace

std::span<const BundledKit> bundled_kits() noexcept {
    return kKits;
}

std::span<const BundledScript> bundled_training_scripts() noexcept {
    return kScripts;
}

}  // namespace apogee::harness
