#!/usr/bin/env python3
"""Speculative decoding, measured before it is built (backlog item 26k).

Run by hand, never by the suite: it needs real weights, a llama-server built
from the pinned llama.cpp, and a GPU nothing else is drawing on.

    speculative_bench.py --server <llama-server> --model <target.gguf> \
        --method none --method ngram-mod --method draft-simple:<draft.gguf> \
        --method draft-mtp [--label NAME] [--runs 1] [--max-tokens 256]

Each method gets its own server (llama-server takes the speculative type at
start), the same three tasks -- prose, code, and a copy-heavy edit -- greedy,
with thinking off, and reports generation tokens per second, how many drafted
tokens were accepted, and whether the text matches the run with no
speculation: under greedy sampling it must, token for token.
"""

from __future__ import annotations

import argparse
import json
import socket
import statistics
import subprocess
import sys
import time
import urllib.request

TASKS = {
    "prose": "Write a story of about 250 words about a lighthouse keeper who finds a "
    "message in a bottle. Plain prose, no title.",
    "code": "Write a Python function `parse_iso_date(text)` that parses an ISO 8601 date "
    "(YYYY-MM-DD) without importing datetime, raising ValueError on bad input, with a "
    "docstring and three doctest examples. Code only.",
    "edit": "Repeat the following text exactly, changing only every occurrence of the word "
    "'blue' to 'green'. Output the text and nothing else.\n\n"
    + " ".join(
        [
            "The blue door at the end of the hall opened onto a courtyard where the blue "
            "tiles had cracked in the winter, and the gardener, who wore a blue coat "
            "every day of the year, swept the leaves into a pile beside the blue bench."
        ]
        * 4
    ),
}


def free_port() -> int:
    with socket.socket() as probe:
        probe.bind(("127.0.0.1", 0))
        return probe.getsockname()[1]


def post(port: int, body: dict) -> dict:
    request = urllib.request.Request(
        f"http://127.0.0.1:{port}/v1/chat/completions",
        data=json.dumps(body).encode(),
        headers={"Content-Type": "application/json"},
    )
    with urllib.request.urlopen(request, timeout=900) as response:
        return json.loads(response.read())


def wait_ready(port: int, server: subprocess.Popen, deadline: float) -> None:
    while time.monotonic() < deadline:
        if server.poll() is not None:
            raise RuntimeError(f"llama-server exited ({server.returncode})")
        try:
            with urllib.request.urlopen(f"http://127.0.0.1:{port}/health", timeout=2) as reply:
                if json.loads(reply.read()).get("status") == "ok":
                    return
        except Exception:  # noqa: BLE001 -- not up yet
            pass
        time.sleep(1)
    raise RuntimeError("llama-server did not become ready")


def server_args(method: str) -> list[str]:
    if method == "none":
        return []
    if method.startswith("draft-simple:"):
        draft = method.split(":", 1)[1]
        return ["--spec-type", "draft-simple", "--spec-draft-model", draft, "-ngld", "99"]
    return ["--spec-type", method]


def measure(args: argparse.Namespace, method: str) -> dict:
    port = free_port()
    command = [args.server, "-m", args.model, "-ngl", "99", "-c", "8192", "--jinja",
               "--port", str(port), "--no-webui", "-np", "1"] + server_args(method)
    server = subprocess.Popen(command, stdout=subprocess.DEVNULL, stderr=subprocess.PIPE,
                              text=True, errors="replace")
    results = {}
    try:
        wait_ready(port, server, time.monotonic() + 600)
        # One short request first, on nothing the tasks share: the first
        # generation after a load pays one-time costs.
        post(port, {"messages": [{"role": "user", "content": "Say hello in five words."}],
                    "max_tokens": 16, "temperature": 0, "cache_prompt": False,
                    "chat_template_kwargs": {"enable_thinking": False,
                                             "reasoning_effort": "low"}})
        for task, prompt in TASKS.items():
            speeds, accepted, drafted, text = [], 0, 0, ""
            for _ in range(args.runs):
                reply = post(port, {
                    "messages": [{"role": "user", "content": prompt}],
                    "max_tokens": args.max_tokens,
                    "temperature": 0,
                    "seed": 1,
                    "cache_prompt": False,
                    "chat_template_kwargs": {"enable_thinking": False,
                                             "reasoning_effort": "low"},
                })
                timings = reply.get("timings", {})
                speeds.append(timings.get("predicted_per_second", 0.0))
                drafted += timings.get("draft_n", 0)
                accepted += timings.get("draft_n_accepted", 0)
                text = reply["choices"][0]["message"].get("content") or ""
            results[task] = {
                "tok_s": statistics.median(speeds),
                "acceptance": (accepted / drafted) if drafted else None,
                "text": text,
            }
    finally:
        server.terminate()
        try:
            server.wait(timeout=30)
        except subprocess.TimeoutExpired:
            server.kill()
    return results


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("--server", required=True)
    parser.add_argument("--model", required=True)
    parser.add_argument("--method", action="append", required=True)
    parser.add_argument("--label", default="")
    # One run per task: a second would let an n-gram drafter copy the first
    # run's answer, which measures the repeat rather than the method.
    parser.add_argument("--runs", type=int, default=1)
    parser.add_argument("--max-tokens", type=int, default=256)
    args = parser.parse_args()

    baseline = None
    print(f"== {args.label or args.model}")
    for method in args.method:
        try:
            results = measure(args, method)
        except Exception as error:  # noqa: BLE001 -- a method that will not run is a result
            print(f"  {method:<16} FAILED: {error}")
            continue
        if method == "none":
            baseline = results
        row = []
        for task, result in results.items():
            speed = result["tok_s"]
            ratio = ""
            same = ""
            if baseline is not None and method != "none":
                base = baseline[task]["tok_s"]
                ratio = f" {speed / base:.2f}x" if base else ""
                same = " same" if result["text"] == baseline[task]["text"] else " DIFFERS"
            acceptance = (f" {result['acceptance'] * 100:.0f}% accepted"
                          if result["acceptance"] is not None else "")
            row.append(f"{task} {speed:.1f} tok/s{ratio}{acceptance}{same}")
        print(f"  {method.split(':')[0]:<16} " + " | ".join(row), flush=True)
    return 0


if __name__ == "__main__":
    sys.exit(main())
