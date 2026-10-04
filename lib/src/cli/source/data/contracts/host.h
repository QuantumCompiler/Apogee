#pragma once

#include <optional>
#include <span>
#include <string>
#include <string_view>

/// What a website's host is, in one place.
///
/// `tools.allowed_hosts` is a list of hosts, the gate asks about a host, and
/// `fetch_url` connects to one. Those three must agree on what "the same host"
/// means, or the guard is a string comparison an attacker gets to choose the
/// other side of: `docs.python.org.evil.example` must never pass for
/// `docs.python.org`, and `DOCS.python.org.` must. So the comparison is on a
/// canonical form made here and nowhere else -- never a substring, never a
/// suffix, never a pattern.
namespace apogee::harness {

/// The canonical form of `host`, or nullopt when it is not a bare host name.
///
/// Accepted: a DNS name (ASCII letters, digits, `-` and `_` in dot-separated
/// labels of at most 63 bytes, 253 in all), an IPv4 address, or an IPv6
/// address with or without its URL brackets. The canonical form is lowercase,
/// without one trailing dot (`example.com.` is `example.com` to DNS), and
/// without IPv6 brackets.
///
/// Refused: anything with a scheme, a port, a path, userinfo, a wildcard, a
/// percent escape, whitespace, or a non-ASCII byte. A non-ASCII name is
/// written in its ASCII (`xn--`) form; accepting both would make two spellings
/// of one host.
[[nodiscard]] std::optional<std::string> canonical_host(std::string_view host);

/// Whether `host` is one of `allowed`, both compared in canonical form. An
/// entry that is not a host (a URL someone pasted) matches nothing; `check`
/// is where it gets reported.
[[nodiscard]] bool host_listed(std::span<const std::string> allowed, std::string_view host);

}  // namespace apogee::harness
