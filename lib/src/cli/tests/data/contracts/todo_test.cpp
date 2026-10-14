#include "contracts/todo.h"

#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

#include <string>
#include <vector>

/// The session todo's floor (41d): the states' words, the lines every text
/// surface draws, and the document every machine face carries -- read back
/// leniently, so a session file degrades and never fails to load.
namespace {

namespace h = apogee::harness;

const h::TodoList kList{{.text = "Read the failing test", .state = h::TodoState::Done},
                        {.text = "Fix the parser", .state = h::TodoState::Active},
                        {.text = "Run the tests", .state = h::TodoState::Pending}};

}  // namespace

TEST_CASE("a todo's states are three words, each way", "[contracts][todo]") {
    for (const h::TodoState state :
         {h::TodoState::Pending, h::TodoState::Active, h::TodoState::Done}) {
        CHECK(h::todo_state_from_string(h::to_string(state)) == state);
    }
    CHECK(h::to_string(h::TodoState::Active) == "active");
    CHECK_FALSE(h::todo_state_from_string("doing").has_value());
    CHECK_FALSE(h::todo_state_from_string("").has_value());
}

TEST_CASE("every text surface draws the same lines", "[contracts][todo]") {
    CHECK(h::describe_todo(kList) ==
          std::vector<std::string>{"todo · 1 of 3 done", "  [x] Read the failing test",
                                   "  [>] Fix the parser", "  [ ] Run the tests"});
    CHECK(h::describe_todo({}).empty());
}

TEST_CASE("the document round-trips, and a bad entry is left out, never fatal",
          "[contracts][todo]") {
    const nlohmann::json json = h::todo_to_json(kList);
    CHECK(json == nlohmann::json::parse(R"([{"text":"Read the failing test","state":"done"},)"
                                        R"({"text":"Fix the parser","state":"active"},)"
                                        R"({"text":"Run the tests","state":"pending"}])"));
    CHECK(h::todo_from_json(json) == kList);
    CHECK(h::todo_from_json(nlohmann::json::parse(
              R"([{"text":"kept","state":"done"},{"text":"no state"},{"state":"done"},)"
              R"({"text":"bad","state":"doing"},"a string",{"text":7,"state":"done"}])")) ==
          h::TodoList{{.text = "kept", .state = h::TodoState::Done}});
    CHECK(h::todo_from_json(nlohmann::json::object()).empty());
}
