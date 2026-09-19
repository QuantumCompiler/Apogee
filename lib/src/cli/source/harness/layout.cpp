#include "harness/layout.h"

#include <array>
#include <system_error>

#include "harness/assets.h"
#include "harness/paths.h"

namespace apogee::harness {
namespace {

/// THE list. Everything else in this file, and every install path, reads it.
///
/// Order matters only for readability -- `create_directories` handles nesting
/// -- but keeping it stable keeps `apogee check` output stable, and diffing
/// two installs is easier when both enumerate in the same order.
constexpr std::array<LayoutEntry, 13> kDirectories{{
    // `config` is a row like any other, even though `config_dir()` is the
    // accessor callers use. Leaving it out was the first version, and it
    // immediately produced the bug this whole file exists to prevent: seeding
    // created it as a special case, `check --fix` enumerated the rows and did
    // not, and the two install paths disagreed by one directory. It is private
    // because a config may carry an inline API key.
    {"config", "config.yaml and anything else the config engine owns", true, true},
    {"logs", "operational log, one file per day", false, true},
    {"sessions", "persisted chat transcripts", true, true},
    {"models", "GGUF models you supply -- Apogee ships and downloads none", false, true},
    {"embeddings", "vector stores for RAG", false, true},
    {"notes", "notes the model keeps for you (the write_note / read_note tools)", false, true},
    {"mcp", "MCP servers scaffolded by 'apogee mcp create', one directory each", false, true},
    {"prompts", "agents' system prompts, loaded by 'apogee analyze --agent'", false, true},
    {"schemas", "agents' output schemas (JSON Schema), loaded beside the prompts", false, true},
    {"analyses", "reports saved by 'apogee analyze', one directory per agent", false, true},
    {"knowledge", "captured decision records' raw conversations (knowledge/raw)", true, true},
    {"training",
     "training datasets, kits, scripts and the Python environment ('apogee datasets' / 'apogee "
     "train')",
     true, true},
    {"cache", "downloads and scratch state, safe to delete", false, false},
}};

}  // namespace

std::span<const LayoutEntry> data_directories() noexcept {
    return kDirectories;
}

std::filesystem::path sessions_dir() {
    return apogee_home() / "sessions";
}

std::filesystem::path logs_dir() {
    return apogee_home() / "logs";
}

std::filesystem::path models_dir() {
    return apogee_home() / "models";
}

std::filesystem::path embeddings_dir() {
    return apogee_home() / "embeddings";
}

std::filesystem::path notes_dir() {
    return apogee_home() / "notes";
}

std::filesystem::path mcp_servers_dir() {
    return apogee_home() / "mcp";
}

std::filesystem::path cache_dir() {
    return apogee_home() / "cache";
}

std::filesystem::path training_dir() {
    return apogee_home() / "training";
}

std::filesystem::path training_datasets_dir() {
    return training_dir() / "datasets";
}

std::filesystem::path training_raw_datasets_dir() {
    return training_datasets_dir() / "raw";
}

std::filesystem::path training_kits_dir() {
    return training_dir() / "kits";
}

std::filesystem::path training_scripts_dir() {
    return training_dir() / "scripts";
}

std::filesystem::path training_venv_dir() {
    return training_dir() / "venv";
}

std::filesystem::path prompts_dir() {
    return apogee_home() / "prompts";
}

std::filesystem::path schemas_dir() {
    return apogee_home() / "schemas";
}

std::filesystem::path analyses_dir() {
    return apogee_home() / "analyses";
}

std::filesystem::path knowledge_dir() {
    return apogee_home() / "knowledge";
}

std::filesystem::path knowledge_raw_dir() {
    return knowledge_dir() / "raw";
}

bool supports_private_modes() noexcept {
#if defined(_WIN32)
    return false;
#else
    return true;
#endif
}

SeedResult seed_data_directory() {
    try {
        return seed_data_directory(apogee_home());
    } catch (const std::exception& e) {
        SeedResult result;
        result.error = e.what();
        return result;
    }
}

SeedResult seed_data_directory(const std::filesystem::path& root) {
    SeedResult result;

    // Only the root is handled outside the rows -- it is the thing they are
    // relative to, not a member of the list.
    const auto make = [&result](const std::filesystem::path& path,
                                const std::string& label) -> bool {
        std::error_code code;
        const bool existed = std::filesystem::exists(path, code);
        if (!existed) {
            std::filesystem::create_directories(path, code);
            if (code) {
                result.error = "could not create " + path.string() + ": " + code.message();
                return false;
            }
            result.created.push_back(label);
        }
        return true;
    };

    if (!make(root, ".")) {
        return result;
    }

    for (const LayoutEntry& entry : data_directories()) {
        if (!make(root / entry.relative_path, std::string{entry.relative_path})) {
            return result;
        }
    }

#if !defined(_WIN32)
    // Tighten what may hold secrets. Done after creation rather than via a
    // umask dance so the result does not depend on the caller's environment --
    // an installer run under a permissive umask must still produce 0700.
    for (const LayoutEntry& entry : data_directories()) {
        if (!entry.private_mode) {
            continue;
        }
        std::error_code code;
        std::filesystem::permissions(root / entry.relative_path, std::filesystem::perms::owner_all,
                                     std::filesystem::perm_options::replace, code);
        if (code) {
            result.error = "could not set permissions on " + (root / entry.relative_path).string() +
                           ": " + code.message();
            return result;
        }
    }
#endif

    // The bundled agents' files, skip-if-present, through the one seeding
    // path -- so `check --fix` and both installers materialise them alike.
    AssetSeedResult assets = seed_bundled_assets(root);
    for (std::string& file : assets.created) {
        result.created.push_back(std::move(file));
    }
    if (!assets.ok()) {
        result.error = assets.error;
    }

    return result;
}

}  // namespace apogee::harness
