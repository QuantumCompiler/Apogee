#include "scaffold/mcp_server.h"

#include <algorithm>
#include <stdexcept>
#include <string_view>
#include <system_error>
#include <vector>

#include "contracts/config.h"
#include "contracts/config_edit.h"
#include "contracts/layout.h"

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

/// A refusal's words about `mapped`, its first mention naming the typed form
/// beside it when the sanitizer changed the name (CLI-M52), as the agent
/// scaffold says it.
std::string naming_both(std::string words, std::string_view typed, std::string_view mapped) {
    const std::string quoted = "'" + std::string{mapped} + "'";
    if (const std::size_t at = words.find(quoted); typed != mapped && at != std::string::npos) {
        words.replace(at, quoted.size(), "'" + std::string{typed} + "' (as " + quoted + ")");
    }
    return words;
}

/// The config's text as the locked edit will read it, parsed as the edit
/// parses it first, for the dry run of its add (CLI-M52; the agent
/// scaffold's twin): a missing or broken config is refused before the
/// folder exists. Only `edit_config_file` writes.
std::string config_text_as_edited(const std::filesystem::path& config_path) {
    std::string text = harness::read_config_file(harness::config_edit_target(config_path));
    (void)harness::parse_config(text, config_path.string());
    return text;
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
                                  const McpServerSpec& spec, std::chrono::milliseconds lock_wait) {
    McpServerResult result;
    result.name = sanitize_server_name(spec.name);
    result.config_path = config_path;

    harness::McpServerConfig entry;
    entry.enabled = true;
    if (!spec.command.empty()) {
        // Register-only: the executable exists already, in any language.
        entry.command = spec.command;
        entry.args = spec.args;
        result.command = entry.command;
        try {
            harness::edit_config_file(
                config_path,
                [&](std::string_view content) {
                    return harness::append_mcp_server(content, result.name, entry, spec.force);
                },
                lock_wait);
        } catch (const std::exception& e) {
            throw std::runtime_error(std::string{"updating config: "} + e.what());
        }
        return result;
    }

    // The scaffold lives beside the config's data directory: the layout's
    // `mcp/` row, derived from the config path so a temp tree stays hermetic.
    const std::filesystem::path home = config_path.parent_path().parent_path();
    result.directory = home / "mcp" / result.name;
    entry.command = (result.directory / "server.py").string();
    result.command = entry.command;

    // A refusal the config is certain to give writes nothing (CLI-M52): the
    // config read as the edit reads it, the cap on a name the add would not
    // replace, then the one add run on the text and kept nowhere, all before
    // the folder.
    const std::string text = config_text_as_edited(config_path);
    try {
        const std::vector<std::string> entries = harness::section_entry_names(text, "mcp_servers");
        const bool replacing = std::ranges::find(entries, result.name) != entries.end() ||
                               harness::fold_collision(entries, result.name).has_value();
        if (!replacing && result.name.size() > kNewServerNameMax) {
            throw std::runtime_error(
                "MCP server name '" + result.name + "' is " + std::to_string(result.name.size()) +
                " bytes -- a new name is at most " + std::to_string(kNewServerNameMax));
        }
        (void)harness::parse_edited_config(
            harness::append_mcp_server(text, result.name, entry, spec.force));
    } catch (const std::exception& e) {
        throw std::runtime_error(naming_both(e.what(), spec.name, result.name));
    }

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
    // From here a failure -- a write, a lock held past the wait, an edit
    // raced in between -- leaves the folder, and says every file in it.
    std::vector<std::string> written;
    bool editing = false;
    try {
        write_file(result.directory / "server.py", python_server_template(result.name), true);
        written.emplace_back("server.py");
        write_file(result.directory / "test_server.py", python_test_template(result.name), false);
        written.emplace_back("test_server.py");
        write_file(result.directory / "README.md", readme_template(result.name), false);
        written.emplace_back("README.md");
        editing = true;
        harness::edit_config_file(
            config_path,
            [&](std::string_view content) {
                return harness::append_mcp_server(content, result.name, entry, spec.force);
            },
            lock_wait);
    } catch (const std::exception& e) {
        std::string files;
        for (std::size_t i = 0; i < written.size(); ++i) {
            files += i == 0 ? "" : (i + 1 == written.size() ? " and " : ", ");
            files += written[i];
        }
        throw std::runtime_error(
            (editing ? std::string{"updating config: "} : std::string{}) + e.what() +
            (files.empty()
                 ? " (nothing was written into the scaffold's folder, " + result.directory.string()
                 : " (the scaffold was written to " + result.directory.string() + ": " + files) +
            "; run it again with --force to replace it)");
    }
    return result;
}

}  // namespace apogee::scaffold
