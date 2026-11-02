#include "acp/host_servers.h"

#include <nlohmann/json.hpp>

#include <cstdint>
#include <fstream>
#include <random>
#include <sstream>
#include <system_error>
#include <utility>

namespace apogee::acp {

namespace {

constexpr std::string_view kFolderPrefix = "apogee-acp-";
constexpr std::string_view kFileName = "servers.json";

[[nodiscard]] std::string string_field(const nlohmann::json& object, std::string_view key) {
    const auto found = object.find(std::string{key});
    return found != object.end() && found->is_string() ? found->get<std::string>() : std::string{};
}

/// The name a note uses for an entry: as the host gave it, or a stand-in.
[[nodiscard]] std::string shown(const std::string& name) {
    return name.empty() ? std::string{"(unnamed)"} : name;
}

/// Why `entry`'s `env` cannot be taken, or empty with `env` filled. Says no
/// value, and no name it refuses -- a name holding `=` may hold a value.
[[nodiscard]] std::string read_env(const nlohmann::json& entry, std::vector<std::string>& env) {
    const auto found = entry.find("env");
    if (found == entry.end() || found->is_null()) {
        return {};
    }
    if (!found->is_array()) {
        return "its env is not a list of {name, value}";
    }
    for (const nlohmann::json& variable : *found) {
        if (!variable.is_object()) {
            return "its env is not a list of {name, value}";
        }
        const auto name = variable.find("name");
        const auto value = variable.find("value");
        if (name == variable.end() || !name->is_string() || value == variable.end() ||
            !value->is_string()) {
            return "its env is not a list of {name, value}";
        }
        const std::string key = name->get<std::string>();
        if (key.empty() || key.find('=') != std::string::npos) {
            return "an env variable's name is empty or holds '='";
        }
        env.push_back(key + "=" + value->get<std::string>());
    }
    return {};
}

/// Why a stdio `entry` cannot be taken, or empty with `server` filled.
[[nodiscard]] std::string read_stdio(const nlohmann::json& entry, HostServer& server) {
    server.command = string_field(entry, "command");
    if (server.command.empty()) {
        return "it names no command";
    }
    if (const auto args = entry.find("args"); args != entry.end() && !args->is_null()) {
        if (!args->is_array()) {
            return "its args are not a list of strings";
        }
        for (const nlohmann::json& arg : *args) {
            if (!arg.is_string()) {
                return "its args are not a list of strings";
            }
            server.args.push_back(arg.get<std::string>());
        }
    }
    return read_env(entry, server.env);
}

[[nodiscard]] std::string random_hex(std::size_t digits) {
    static constexpr std::string_view kHex = "0123456789abcdef";
    std::random_device entropy;
    std::mt19937_64 random{(static_cast<std::uint64_t>(entropy()) << 32U) ^ entropy()};
    std::string out;
    std::uint64_t value = random();
    for (std::size_t i = 0; i < digits; ++i) {
        if (i % 16 == 0 && i > 0) {
            value = random();
        }
        out += kHex[static_cast<std::size_t>(value & 0xFU)];
        value >>= 4U;
    }
    return out;
}

/// One server of a handover, as `write_handover` wrote it: strings kept,
/// anything else dropped.
[[nodiscard]] HostServer read_server(const nlohmann::json& entry) {
    HostServer server;
    server.name = string_field(entry, "name");
    server.command = string_field(entry, "command");
    for (const char* list : {"args", "env"}) {
        const auto found = entry.find(list);
        if (found == entry.end() || !found->is_array()) {
            continue;
        }
        std::vector<std::string>& target =
            std::string_view{list} == "args" ? server.args : server.env;
        for (const nlohmann::json& item : *found) {
            if (item.is_string()) {
                target.push_back(item.get<std::string>());
            }
        }
    }
    return server;
}

/// Whether `file` sits where `write_handover` puts one.
[[nodiscard]] bool made_here(const std::filesystem::path& file) {
    return file.filename() == kFileName &&
           file.parent_path().filename().string().starts_with(kFolderPrefix);
}

}  // namespace

HostServers host_servers(const nlohmann::json& mcp_servers) {
    HostServers taken;
    if (!mcp_servers.is_array()) {
        return taken;
    }
    std::vector<std::string> remote;
    for (const nlohmann::json& entry : mcp_servers) {
        if (!entry.is_object()) {
            taken.notes.emplace_back("an MCP server entry that is not an object was skipped");
            continue;
        }
        HostServer server;
        server.name = string_field(entry, "name");
        const std::string type = string_field(entry, "type");
        if (!type.empty() && type != "stdio") {
            remote.push_back(shown(server.name));
            continue;
        }
        if (const std::string refused = read_stdio(entry, server); !refused.empty()) {
            taken.notes.push_back("the host's MCP server '" + shown(server.name) +
                                  "' was skipped: " + refused);
            continue;
        }
        taken.servers.push_back(std::move(server));
    }
    if (!remote.empty()) {
        std::string names;
        for (const std::string& name : remote) {
            names += (names.empty() ? "" : ", ") + name;
        }
        taken.notes.insert(taken.notes.begin(),
                           "Apogee connects stdio MCP servers only; skipped: " + names);
    }
    return taken;
}

std::string describe(const HostServers& servers) {
    std::string out;
    for (const HostServer& server : servers.servers) {
        out += (out.empty() ? "" : ", ") + shown(server.name) + " (" + server.command;
        std::string keys;
        for (const std::string& entry : server.env) {
            keys += (keys.empty() ? "" : ", ") + entry.substr(0, entry.find('='));
        }
        if (!keys.empty()) {
            out += "; env " + keys;
        }
        out += ")";
    }
    return out;
}

std::filesystem::path write_handover(const HostServers& servers,
                                     const std::filesystem::path& parent, std::string& error) {
    std::error_code code;
    const std::filesystem::path root =
        parent.empty() ? std::filesystem::temp_directory_path(code) : parent;
    if (code) {
        error = "no temporary folder: " + code.message();
        return {};
    }
    // A fresh folder, never one that was there: a name taken is tried again.
    std::filesystem::path folder;
    for (int attempt = 0; attempt < 8 && folder.empty(); ++attempt) {
        const std::filesystem::path candidate =
            root / (std::string{kFolderPrefix} + random_hex(16));
        if (std::filesystem::create_directory(candidate, code) && !code) {
            folder = candidate;
        }
    }
    if (folder.empty()) {
        error = "no folder could be made under " + root.string() +
                (code ? ": " + code.message() : std::string{});
        return {};
    }
    const auto fail = [&folder, &error](const std::string& why) {
        error = why;
        std::error_code ignored;
        std::filesystem::remove_all(folder, ignored);
        return std::filesystem::path{};
    };
    // Owner-only before anything is in it, then the file owner-only before
    // its first byte: the values are never on disk under the umask's mode.
    std::filesystem::permissions(folder, std::filesystem::perms::owner_all,
                                 std::filesystem::perm_options::replace, code);
    if (code) {
        return fail("cannot make " + folder.string() + " owner-only: " + code.message());
    }
    std::filesystem::path file = folder / kFileName;
    {
        const std::ofstream create{file, std::ios::binary | std::ios::trunc};
        if (!create) {
            return fail("cannot create " + file.string());
        }
    }
    std::filesystem::permissions(
        file, std::filesystem::perms::owner_read | std::filesystem::perms::owner_write,
        std::filesystem::perm_options::replace, code);
    if (code) {
        return fail("cannot make " + file.string() + " owner-only: " + code.message());
    }
    nlohmann::json document{{"servers", nlohmann::json::array()}, {"notes", servers.notes}};
    for (const HostServer& server : servers.servers) {
        document["servers"].push_back({{"name", server.name},
                                       {"command", server.command},
                                       {"args", server.args},
                                       {"env", server.env}});
    }
    std::ofstream out{file, std::ios::binary | std::ios::trunc};
    out << document.dump();
    out.flush();
    if (!out) {
        return fail("cannot write " + file.string());
    }
    return file;
}

HostServers read_handover(const std::filesystem::path& file, std::string& error) {
    std::string text;
    {
        const std::ifstream in{file, std::ios::binary};
        if (!in) {
            error = "cannot read " + file.string();
            return {};
        }
        std::ostringstream buffer;
        buffer << in.rdbuf();
        text = buffer.str();
    }
    // Read once, gone: what it holds is in this process alone from here on.
    discard_handover(file);
    const nlohmann::json document = nlohmann::json::parse(text, nullptr, false);
    if (document.is_discarded() || !document.is_object()) {
        error = file.string() + " is not a handover of MCP servers";
        return {};
    }
    HostServers read;
    if (const auto notes = document.find("notes"); notes != document.end() && notes->is_array()) {
        for (const nlohmann::json& note : *notes) {
            if (note.is_string()) {
                read.notes.push_back(note.get<std::string>());
            }
        }
    }
    const auto servers = document.find("servers");
    if (servers == document.end() || !servers->is_array()) {
        return read;
    }
    for (const nlohmann::json& entry : *servers) {
        if (entry.is_object()) {
            read.servers.push_back(read_server(entry));
        }
    }
    return read;
}

void discard_handover(const std::filesystem::path& file) noexcept {
    try {
        if (file.empty() || !made_here(file)) {
            return;
        }
        std::error_code ignored;
        std::filesystem::remove(file, ignored);
        std::filesystem::remove(file.parent_path(), ignored);
    } catch (...) {
        // Quiet by contract: a path that cannot even be looked at is left.
        return;
    }
}

}  // namespace apogee::acp
