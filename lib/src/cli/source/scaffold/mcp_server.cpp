#include "scaffold/mcp_server.h"

#include <stdexcept>
#include <system_error>

#include "harness/config.h"
#include "harness/config_edit.h"
#include "harness/layout.h"

namespace apogee::scaffold {
namespace {

constexpr std::string_view kServerTemplate = R"PY(#!/usr/bin/env python3
"""__NAME__ -- a local MCP server scaffolded by `apogee mcp create`.

Tools: echo  (replace with your own)

Wire format: JSON-RPC 2.0 over stdin/stdout (the MCP stdio transport). Edit the
TOOLS manifest and DISPATCH below to add tools, then try:
    apogee mcp test __NAME__ echo '{"text":"hi"}'

Everything you print to stderr is captured by Apogee and shown only when a
connection fails; stdout is JSON-RPC and nothing else.
"""
import json
import sys


def send(obj):
    sys.stdout.write(json.dumps(obj) + "\n")
    sys.stdout.flush()


def ok(msg_id, text):
    send({"jsonrpc": "2.0", "id": msg_id,
          "result": {"content": [{"type": "text", "text": text}], "isError": False}})


def err(msg_id, text):
    send({"jsonrpc": "2.0", "id": msg_id,
          "result": {"content": [{"type": "text", "text": "Error: " + text}], "isError": True}})


# -- tool implementations -----------------------------------------------------

def tool_echo(args):
    return "echo: " + str(args.get("text", ""))


DISPATCH = {
    "echo": tool_echo,
}

# -- MCP tool manifest --------------------------------------------------------

TOOLS = [
    {
        "name": "echo",
        "description": "Echo back the provided text. Replace with your own tool.",
        "inputSchema": {
            "type": "object",
            "properties": {"text": {"type": "string", "description": "Text to echo"}},
            "required": ["text"],
        },
        "annotations": {"readOnlyHint": True},
    },
]

# -- main loop ----------------------------------------------------------------

for line in sys.stdin:
    line = line.strip()
    if not line:
        continue
    try:
        msg = json.loads(line)
    except json.JSONDecodeError:
        continue

    method = msg.get("method")
    msg_id = msg.get("id")

    if method == "initialize":
        send({"jsonrpc": "2.0", "id": msg_id, "result": {
            "protocolVersion": "2025-03-26",
            "capabilities": {"tools": {}},
            "serverInfo": {"name": "__NAME__", "version": "0.1.0"},
        }})
    elif method == "notifications/initialized":
        pass  # a notification: no response
    elif method == "tools/list":
        send({"jsonrpc": "2.0", "id": msg_id, "result": {"tools": TOOLS}})
    elif method == "tools/call":
        params = msg.get("params", {})
        name = params.get("name", "")
        args = params.get("arguments") or {}
        fn = DISPATCH.get(name)
        if fn is None:
            err(msg_id, "unknown tool: " + name)
            continue
        try:
            ok(msg_id, fn(args))
        except Exception as exc:  # noqa: BLE001
            err(msg_id, str(exc))
    elif msg_id is not None:
        send({"jsonrpc": "2.0", "id": msg_id,
              "error": {"code": -32601, "message": "method not found: " + str(method)}})
)PY";

constexpr std::string_view kTestTemplate = R"PY(#!/usr/bin/env python3
"""Smoke test for __NAME__: starts the server and exercises the echo tool over
JSON-RPC 2.0 on stdio. Run:  python3 test_server.py"""
import json
import os
import subprocess
import sys
import unittest

_HERE = os.path.dirname(os.path.abspath(__file__))
_SERVER = os.path.join(_HERE, "server.py")


def rpc(proc, msg_id, method, params):
    proc.stdin.write(json.dumps({"jsonrpc": "2.0", "id": msg_id,
                                 "method": method, "params": params}) + "\n")
    proc.stdin.flush()
    while True:
        line = proc.stdout.readline()
        if not line:
            raise EOFError("server closed stdout")
        resp = json.loads(line)
        if resp.get("id") == msg_id:
            return resp


class TestServer(unittest.TestCase):
    def setUp(self):
        self.proc = subprocess.Popen([sys.executable, _SERVER],
                                     stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                                     stderr=subprocess.DEVNULL, text=True)
        rpc(self.proc, 1, "initialize", {"protocolVersion": "2025-03-26",
                                         "capabilities": {},
                                         "clientInfo": {"name": "test", "version": "0"}})

    def tearDown(self):
        self.proc.stdin.close()
        self.proc.wait(timeout=5)

    def test_tools_list(self):
        resp = rpc(self.proc, 2, "tools/list", {})
        names = {t["name"] for t in resp["result"]["tools"]}
        self.assertIn("echo", names)

    def test_echo(self):
        resp = rpc(self.proc, 3, "tools/call", {"name": "echo", "arguments": {"text": "hi"}})
        self.assertFalse(resp["result"].get("isError"), resp)
        self.assertIn("hi", resp["result"]["content"][0]["text"])


if __name__ == "__main__":
    unittest.main(verbosity=2)
)PY";

constexpr std::string_view kReadmeTemplate = R"MD(# __NAME__

A local MCP server scaffolded by `apogee mcp create`, registered in your
config under `mcp_servers:` and available to every tool-using run
(`--tools` on `complete`, `chat`, and `serve`).

## Try it

    apogee mcp test __NAME__ echo '{"text":"hi"}'
    apogee mcp list

## Add a tool

Edit `server.py`: add an entry to the `TOOLS` manifest and a matching function
in `DISPATCH`. Re-run `apogee mcp test` to verify -- nothing to reinstall. A
tool the model may call without asking should carry
`"annotations": {"readOnlyHint": true}`; without it, Apogee treats the tool as
destructive and asks the user before running it.

Switch it off with `apogee mcp disable __NAME__`; remove it with
`apogee config delete-mcp-server __NAME__`.
)MD";

std::string render(std::string_view tmpl, std::string_view name) {
    std::string out{tmpl};
    const std::string needle = "__NAME__";
    std::size_t at = out.find(needle);
    while (at != std::string::npos) {
        out.replace(at, needle.size(), name);
        at = out.find(needle, at + name.size());
    }
    return out;
}

void write_file(const std::filesystem::path& path, std::string_view content, bool executable) {
    harness::write_file_atomically(path, content);
    if (executable && harness::supports_private_modes()) {
        std::error_code code;
        std::filesystem::permissions(path,
                                     std::filesystem::perms::owner_exec |
                                         std::filesystem::perms::group_exec |
                                         std::filesystem::perms::others_exec,
                                     std::filesystem::perm_options::add, code);
    }
}

}  // namespace

std::string sanitize_server_name(std::string_view name) {
    std::string out;
    for (const char c : name) {
        if (c == '.' || c == ':') {
            out += '-';
        } else {
            out += c;
        }
    }
    for (const char c : out) {
        const bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                        (c >= '0' && c <= '9') || c == '_' || c == '-';
        if (!ok) {
            throw std::runtime_error("'" + std::string{name} +
                                     "' is not a server name (letters, digits, '_' and '-')");
        }
    }
    if (out.empty() || out.find("__") != std::string::npos) {
        throw std::runtime_error(
            "a server name must be non-empty and must not contain '__' "
            "(it delimits mcp__<server>__<tool>)");
    }
    return out;
}

std::string python_server_template(std::string_view name) {
    return render(kServerTemplate, name);
}

std::string python_test_template(std::string_view name) {
    return render(kTestTemplate, name);
}

std::string readme_template(std::string_view name) {
    return render(kReadmeTemplate, name);
}

McpServerResult create_mcp_server(const std::filesystem::path& config_path,
                                  const McpServerSpec& spec) {
    McpServerResult result;
    result.name = sanitize_server_name(spec.name);
    result.config_path = config_path;

    harness::McpServerConfig entry;
    entry.enabled = true;
    if (!spec.command.empty()) {
        // Register-only: the executable exists already, in any language.
        entry.command = spec.command;
        entry.args = spec.args;
    } else {
        // The scaffold lives beside the config's data directory: the layout's
        // `mcp/` row, derived from the config path so a temp tree stays
        // hermetic.
        const std::filesystem::path home = config_path.parent_path().parent_path();
        result.directory = home / "mcp" / result.name;
        std::error_code code;
        if (std::filesystem::exists(result.directory, code) && !spec.force) {
            throw std::runtime_error("MCP server directory already exists: " +
                                     result.directory.string() + " (use --force to overwrite)");
        }
        std::filesystem::create_directories(result.directory, code);
        if (code) {
            throw std::runtime_error("could not create " + result.directory.string() + ": " +
                                     code.message());
        }
        write_file(result.directory / "server.py", python_server_template(result.name), true);
        write_file(result.directory / "test_server.py", python_test_template(result.name), false);
        write_file(result.directory / "README.md", readme_template(result.name), false);
        entry.command = (result.directory / "server.py").string();
    }
    result.command = entry.command;

    try {
        harness::edit_config_file(config_path, [&](std::string_view content) {
            return harness::append_mcp_server(content, result.name, entry, spec.force);
        });
    } catch (const std::exception& e) {
        // The files were written; say so rather than hiding it.
        throw std::runtime_error(std::string{"updating config: "} + e.what() +
                                 (result.directory.empty() ? ""
                                                           : " (the scaffold was written to " +
                                                                 result.directory.string() + ")"));
    }
    return result;
}

}  // namespace apogee::scaffold
