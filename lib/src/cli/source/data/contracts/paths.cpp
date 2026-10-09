#include "contracts/paths.h"

#include <atomic>
#include <cstdlib>
#include <mutex>
#include <stdexcept>
#include <system_error>
#include <utility>

#include "platform/platform.h"

namespace apogee::harness {
namespace {

std::atomic<Channel>& baked() noexcept {
    static std::atomic<Channel> channel{Channel::Release};
    return channel;
}

/// The root flag in force, behind one lock: written once per run by the root
/// command, read on every layout lookup -- `serve` reads from several threads.
struct FlagSlot {
    std::mutex mutex;
    std::optional<RootFlag> flag;
};

FlagSlot& slot() {
    static FlagSlot instance;
    return instance;
}

/// `path` in the form two spellings of one directory share: absolute, with
/// what exists of it resolved through symlinks (`/tmp` and `/private/tmp` on
/// macOS), and no trailing separator.
[[nodiscard]] std::filesystem::path comparable(const std::filesystem::path& path) {
    std::error_code code;
    std::filesystem::path absolute = std::filesystem::absolute(path, code);
    if (code) {
        absolute = path;
    }
    std::filesystem::path out = std::filesystem::weakly_canonical(absolute, code);
    if (code) {
        out = absolute.lexically_normal();
    }
    if (out.has_relative_path() && out.filename().empty()) {
        out = out.parent_path();
    }
    return out;
}

}  // namespace

Channel baked_channel() noexcept {
    return baked().load();
}

void set_baked_channel(Channel channel) noexcept {
    baked().store(channel);
}

std::string RootFlag::spelling() const {
    if (channel.has_value()) {
        return std::string{channel_flag(*channel)};
    }
    return std::string{kCustomFlag} + " " + custom_config.string();
}

std::string root_reason(const RootResolution& resolution) {
    switch (resolution.rung) {
        case RootRung::Flag:
            return "set by " +
                   (resolution.flag ? resolution.flag->spelling() : std::string{"a flag"});
        case RootRung::Environment:
            return std::string{"set by "} + kHomeEnvVar;
        case RootRung::Baked:
            break;
    }
    return "the " + std::string{channel_name(resolution.channel)} +
           " channel's own root, baked into this build";
}

CustomRoot custom_root(const std::filesystem::path& config_file) {
    CustomRoot out;
    const auto refuse = [&](const std::string& why, const std::string& fix) {
        out.error = std::string{kCustomFlag} + " " + config_file.string() + ": " + why +
                    ". It takes a config file at <root>/config/<file> -- the layout's own place "
                    "for one, as ~/.apogee/config/config.json roots ~/.apogee -- and roots "
                    "everything at <root>" +
                    fix;
    };
    if (config_file.empty()) {
        refuse("no config file named", "");
        return out;
    }

    std::error_code code;
    std::filesystem::path absolute = std::filesystem::absolute(config_file, code);
    if (code) {
        absolute = config_file;
    }
    absolute = absolute.lexically_normal();

    if (absolute.filename().empty() || std::filesystem::is_directory(absolute, code)) {
        refuse("that is a directory, not a config file", "");
        return out;
    }
    const std::filesystem::path directory = absolute.parent_path();
    if (directory.filename() != "config") {
        refuse("it is not in a directory named config, so its position names no root",
               " (as " + (directory / "config" / absolute.filename()).string() + " would root " +
                   directory.string() + ")");
        return out;
    }
    const std::filesystem::path root = home_for_config(absolute);
    if (root.empty() || root == root.root_path()) {
        refuse("that would root Apogee at the top of the filesystem", "");
        return out;
    }
    out.root = root;
    out.config = absolute;
    return out;
}

bool same_path(const std::filesystem::path& left, const std::filesystem::path& right) {
    return comparable(left) == comparable(right);
}

RootResolution resolve_root(const RootInputs& inputs) {
    RootResolution out;
    out.channel = inputs.channel;

    if (inputs.flag.has_value()) {
        const RootFlag& flag = *inputs.flag;
        out.rung = RootRung::Flag;
        out.flag = flag;
        if (flag.channel.has_value()) {
            if (!inputs.home_directory.has_value()) {
                out.error = "cannot determine your home directory, which " + flag.spelling() +
                            " roots Apogee under; name a root with " + std::string{kCustomFlag} +
                            " <root>/config/config.json instead";
                return out;
            }
            out.root = std::filesystem::path{*inputs.home_directory} /
                       std::string{channel_directory(*flag.channel)};
            out.config = config_file_in(out.root / "config");
        } else {
            CustomRoot custom = custom_root(flag.custom_config);
            if (!custom.error.empty()) {
                out.error = std::move(custom.error);
                return out;
            }
            out.root = std::move(custom.root);
            out.config = std::move(custom.config);
        }
        if (!inputs.environment.empty() && !same_path(out.root, inputs.environment)) {
            out.error = flag.spelling() + " roots Apogee at " + out.root.string() + ", but " +
                        kHomeEnvVar + " is " + inputs.environment +
                        " -- a run has one root; unset " + kHomeEnvVar + " or drop the flag";
            out.root.clear();
            out.config.clear();
        }
        return out;
    }

    if (!inputs.environment.empty()) {
        out.rung = RootRung::Environment;
        out.root = std::filesystem::path{inputs.environment};
        out.config = config_file_in(out.root / "config");
        return out;
    }

    if (!inputs.home_directory.has_value()) {
        out.error =
            "cannot determine your home directory; set the APOGEE_HOME "
            "environment variable to choose where Apogee keeps its files";
        return out;
    }
    out.rung = RootRung::Baked;
    out.root =
        std::filesystem::path{*inputs.home_directory} / std::string{channel_directory(out.channel)};
    out.config = config_file_in(out.root / "config");
    return out;
}

RootInputs current_root_inputs() {
    RootInputs inputs;
    inputs.flag = root_flag();
    if (const char* environment = std::getenv(kHomeEnvVar); environment != nullptr) {
        inputs.environment = environment;
    }
    inputs.channel = baked_channel();
    inputs.home_directory = platform::home_directory();
    return inputs;
}

RootResolution current_root() {
    return resolve_root(current_root_inputs());
}

std::optional<RootFlag> root_flag() {
    FlagSlot& flags = slot();
    const std::scoped_lock lock{flags.mutex};
    return flags.flag;
}

RootFlagScope::RootFlagScope(std::optional<RootFlag> flag) {
    FlagSlot& flags = slot();
    const std::scoped_lock lock{flags.mutex};
    previous_ = std::exchange(flags.flag, std::move(flag));
}

RootFlagScope::~RootFlagScope() {
    FlagSlot& flags = slot();
    const std::scoped_lock lock{flags.mutex};
    flags.flag = std::move(previous_);
}

std::filesystem::path apogee_home() {
    RootResolution resolved = current_root();
    if (!resolved.ok()) {
        throw std::runtime_error(resolved.error);
    }
    return std::move(resolved.root);
}

std::filesystem::path install_home() {
    RootInputs inputs = current_root_inputs();
    inputs.flag.reset();
    RootResolution resolved = resolve_root(inputs);
    if (!resolved.ok()) {
        throw std::runtime_error(resolved.error);
    }
    return std::move(resolved.root);
}

std::filesystem::path config_dir() {
    return apogee_home() / "config";
}

std::filesystem::path config_file_in(const std::filesystem::path& config_directory) {
    std::error_code code;
    const std::filesystem::path current = config_directory / kConfigFileName;
    const std::filesystem::path legacy = config_directory / kLegacyConfigFileName;
    if (!std::filesystem::exists(current, code) && std::filesystem::is_regular_file(legacy, code)) {
        return legacy;
    }
    return current;
}

bool is_legacy_config_path(const std::filesystem::path& path) {
    return path.filename() == kLegacyConfigFileName;
}

std::string config_conflict(const std::filesystem::path& config_directory) {
    std::error_code code;
    const std::filesystem::path current = config_directory / kConfigFileName;
    const std::filesystem::path legacy = config_directory / kLegacyConfigFileName;
    if (!std::filesystem::exists(current, code) || !std::filesystem::exists(legacy, code)) {
        return {};
    }
    return "both " + legacy.string() + " and " + current.string() +
           " exist, and which one is current is yours to say -- move the other aside (once "
           "config.yaml is the one, 'apogee config migrate' converts it)";
}

std::filesystem::path default_config_path() {
    RootResolution resolved = current_root();
    if (!resolved.ok()) {
        throw std::runtime_error(resolved.error);
    }
    const bool custom = resolved.flag.has_value() && !resolved.flag->channel.has_value();
    if (!custom) {
        if (std::string conflict = config_conflict(resolved.config.parent_path());
            !conflict.empty()) {
            throw std::runtime_error(conflict);
        }
    }
    return std::move(resolved.config);
}

std::filesystem::path resolve_config_path(const std::string& flag_value) {
    if (!flag_value.empty()) {
        return std::filesystem::path{flag_value};
    }
    return default_config_path();
}

std::filesystem::path home_for_config(const std::filesystem::path& config_path) {
    return config_path.parent_path().parent_path();
}

}  // namespace apogee::harness
