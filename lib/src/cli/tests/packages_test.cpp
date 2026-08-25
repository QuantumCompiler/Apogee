// Every reserved package header compiles and is reachable by its documented
// include path.
//
// Without this, the package stubs are directories nobody compiles: the first
// item to open one discovers a typo'd include guard or a wrong path that has
// been broken since the skeleton landed. Each later item deletes its line here
// only by replacing it with real tests for real code.

#include <catch2/catch_test_macros.hpp>

#include "agentloop/agentloop.h"
#include "backends/backends.h"
#include "embedstore/embedstore.h"
#include "harness/harness.h"
#include "httpserver/httpserver.h"
#include "mcp/mcp.h"

TEST_CASE("reserved package headers are includable from the library target", "[packages]") {
    // Reaching this line is the assertion: the includes above resolved and
    // compiled against apogee_core's public include directory.
    SUCCEED("all reserved package headers compiled");
}
