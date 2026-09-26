#include "harness/host.h"

#include <catch2/catch_test_macros.hpp>

#include <optional>
#include <string>
#include <vector>

using apogee::harness::canonical_host;
using apogee::harness::host_listed;

TEST_CASE("a host has one canonical spelling", "[harness][host]") {
    CHECK(canonical_host("docs.python.org") == std::optional<std::string>{"docs.python.org"});
    CHECK(canonical_host("DOCS.Python.ORG") == std::optional<std::string>{"docs.python.org"});
    // One trailing dot is the same name to DNS.
    CHECK(canonical_host("docs.python.org.") == std::optional<std::string>{"docs.python.org"});
    CHECK(canonical_host("localhost") == std::optional<std::string>{"localhost"});
    CHECK(canonical_host("127.0.0.1") == std::optional<std::string>{"127.0.0.1"});
    CHECK(canonical_host("xn--bcher-kva.example") ==
          std::optional<std::string>{"xn--bcher-kva.example"});
    CHECK(canonical_host("_dmarc.example.com") == std::optional<std::string>{"_dmarc.example.com"});
    // IPv6 with or without the URL's brackets, one key either way.
    CHECK(canonical_host("[::1]") == std::optional<std::string>{"::1"});
    CHECK(canonical_host("::1") == std::optional<std::string>{"::1"});
    CHECK(canonical_host("[2001:DB8::1]") == std::optional<std::string>{"2001:db8::1"});
}

TEST_CASE("anything that is not a bare host is refused", "[harness][host]") {
    for (const char* bad : {"", ".", "https://docs.python.org", "docs.python.org/", "a..b", ".a",
                            "a.b..", "docs.python.org:443", "cafe:80", "user@docs.python.org",
                            "*.python.org", "docs python.org", "docs%2epython.org",
                            "b\xc3\xbc"
                            "cher.example",
                            "[::1%25eth0]", "[]", "docs.python.org\n", "a\\b.example"}) {
        INFO(bad);
        CHECK_FALSE(canonical_host(bad).has_value());
    }
    CHECK_FALSE(canonical_host(std::string(64, 'a') + ".example").has_value());
    CHECK(canonical_host(std::string(63, 'a') + ".example").has_value());
}

TEST_CASE("listing is exact: no prefix, suffix or substring admits a host", "[harness][host]") {
    const std::vector<std::string> allowed{"docs.python.org", "Example.COM.", "https://pypi.org",
                                           "[::1]"};
    CHECK(host_listed(allowed, "docs.python.org"));
    CHECK(host_listed(allowed, "DOCS.PYTHON.ORG."));
    CHECK(host_listed(allowed, "example.com"));
    CHECK(host_listed(allowed, "::1"));
    CHECK(host_listed(allowed, "[::1]"));

    CHECK_FALSE(host_listed(allowed, "python.org"));                    // a parent
    CHECK_FALSE(host_listed(allowed, "evil.docs.python.org"));          // a child
    CHECK_FALSE(host_listed(allowed, "docs.python.org.evil.example"));  // a suffix trick
    CHECK_FALSE(host_listed(allowed, "evildocs.python.org"));           // a prefix trick
    CHECK_FALSE(host_listed(allowed, "docs.python.org:8080"));          // not a host
    CHECK_FALSE(host_listed(allowed, "pypi.org"));  // a pasted URL is not an entry
    CHECK_FALSE(host_listed({}, "docs.python.org"));
}
