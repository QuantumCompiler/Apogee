#include "harness/host.h"

#include <algorithm>
#include <cctype>

namespace apogee::harness {
namespace {

constexpr std::size_t kMaxName = 253;
constexpr std::size_t kMaxLabel = 63;

std::optional<std::string> canonical_ipv6(std::string_view address) {
    // Every IPv6 address has at least two colons; `cafe:80` is a host and a
    // port, whose letters merely happen to be hex.
    if (address.size() > 45 || std::count(address.begin(), address.end(), ':') < 2) {
        return std::nullopt;
    }
    std::string out;
    out.reserve(address.size());
    for (const char c : address) {
        const auto byte = static_cast<unsigned char>(c);
        // Hex digits, colons, and the dots of an embedded IPv4 tail. A zone
        // (`%eth0`) names an interface on this machine, not a website.
        if (std::isxdigit(byte) == 0 && c != ':' && c != '.') {
            return std::nullopt;
        }
        out.push_back(static_cast<char>(std::tolower(byte)));
    }
    return out;
}

std::optional<std::string> canonical_name(std::string_view name) {
    if (!name.empty() && name.back() == '.') {
        name.remove_suffix(1);
    }
    if (name.empty() || name.size() > kMaxName) {
        return std::nullopt;
    }
    std::string out;
    out.reserve(name.size());
    std::size_t label = 0;
    for (const char c : name) {
        const auto byte = static_cast<unsigned char>(c);
        if (c == '.') {
            if (label == 0) {
                return std::nullopt;  // an empty label: `.a`, `a..b`
            }
            label = 0;
            out.push_back('.');
            continue;
        }
        if (std::isalnum(byte) == 0 && c != '-' && c != '_') {
            return std::nullopt;
        }
        if (++label > kMaxLabel) {
            return std::nullopt;
        }
        out.push_back(static_cast<char>(std::tolower(byte)));
    }
    if (label == 0) {
        return std::nullopt;
    }
    return out;
}

}  // namespace

std::optional<std::string> canonical_host(std::string_view host) {
    if (host.size() >= 2 && host.front() == '[' && host.back() == ']') {
        return canonical_ipv6(host.substr(1, host.size() - 2));
    }
    if (host.find(':') != std::string_view::npos) {
        // A bare IPv6 address, as a config entry may spell one. `host:port`
        // fails here too: a port has no business in a host list.
        return canonical_ipv6(host);
    }
    return canonical_name(host);
}

bool host_listed(std::span<const std::string> allowed, std::string_view host) {
    const std::optional<std::string> wanted = canonical_host(host);
    if (!wanted.has_value()) {
        return false;
    }
    for (const std::string& entry : allowed) {
        const std::optional<std::string> listed = canonical_host(entry);
        if (listed.has_value() && *listed == *wanted) {
            return true;
        }
    }
    return false;
}

}  // namespace apogee::harness
