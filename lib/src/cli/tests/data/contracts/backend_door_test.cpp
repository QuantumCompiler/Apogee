#include "contracts/backend_door.h"

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <utility>
#include <vector>

#include "contracts/suggested_command.h"

/// CLI-M50: a door that edits a configured backend leads with the rename
/// that reaches it when the name is one the editors refuse (55f's rule,
/// 57e's refusal), and is today's door, byte for byte, for any other.
namespace {

using apogee::harness::backend_door_remedy;
using apogee::harness::backend_door_said;
using apogee::harness::backend_edit_door;
using apogee::harness::CommandPart;
using apogee::harness::kNewBackendName;
using apogee::harness::operand;
using apogee::harness::option_placeholder;
using apogee::harness::renames_first;
using apogee::harness::render;
using apogee::harness::SuggestedCommand;

[[nodiscard]] SuggestedCommand delete_backend(CommandPart target) {
    return SuggestedCommand{.verbs = {"config", "delete-backend"}, .parts = {std::move(target)}};
}

[[nodiscard]] SuggestedCommand set_backend(CommandPart target) {
    return SuggestedCommand{
        .verbs = {"config", "set-backend"},
        .parts = {std::move(target), option_placeholder("--model-path", "<an existing .gguf>")}};
}

}  // namespace

TEST_CASE("a name both config formats edit gets its door alone, byte for byte",
          "[contracts][backend-door][M50]") {
    for (const std::string name : {"local", "qwen-home", "org/model", "9b", "true"}) {
        INFO("backend [" << name << "]");
        for (const auto door : {&delete_backend, &set_backend}) {
            const std::vector<SuggestedCommand> commands = backend_edit_door(name, *door);
            REQUIRE(commands.size() == 1U);
            CHECK_FALSE(renames_first(commands));
            const std::string alone = render((*door)(operand(name)));
            CHECK(render(commands.front()) == alone);
            CHECK(backend_door_remedy(commands) == alone);
            CHECK(backend_door_said(commands) == alone);
        }
    }
    CHECK(backend_door_said(backend_edit_door("local", set_backend)) ==
          "apogee config set-backend local --model-path <an existing .gguf>");
}

TEST_CASE("a name the editors refuse gets the rename first, then the door on the new name",
          "[contracts][backend-door][M50]") {
    const std::vector<std::pair<std::string, std::string>> renames{
        {"-x", "apogee config rename-backend -- -x <new name>"},
        {"--help", "apogee config rename-backend -- --help <new name>"},
        {"a b", "apogee config rename-backend \"a b\" <new name>"},
        {"a;b", "apogee config rename-backend \"a;b\" <new name>"},
        {"it's", "apogee config rename-backend \"it's\" <new name>"},
        {"qwen@home", "apogee config rename-backend qwen@home <new name>"},
        {"null", "apogee config rename-backend null <new name>"},
        {".x", "apogee config rename-backend .x <new name>"},
    };
    for (const auto& [name, rename] : renames) {
        INFO("backend [" << name << "]");
        const std::vector<SuggestedCommand> commands = backend_edit_door(name, delete_backend);
        REQUIRE(commands.size() == 2U);
        CHECK(renames_first(commands));
        // The old name as given -- the key as stored -- then the placeholder.
        CHECK(render(commands.front()) == rename);
        CHECK(render(commands.back()) == "apogee config delete-backend <new name>");
        CHECK(render(commands.back()) ==
              render(delete_backend(apogee::harness::placeholder(kNewBackendName))));
    }
}

TEST_CASE("the rename door reads as a remedy's chain and as a sentence",
          "[contracts][backend-door][M50]") {
    const std::vector<SuggestedCommand> dashed = backend_edit_door("-x", delete_backend);
    CHECK(backend_door_remedy(dashed) ==
          "apogee config rename-backend -- -x <new name> && apogee config delete-backend <new "
          "name>");
    CHECK(backend_door_said(dashed) ==
          "rename it first: apogee config rename-backend -- -x <new name>, then apogee config "
          "delete-backend <new name>");
    const std::vector<SuggestedCommand> spaced = backend_edit_door("a b", set_backend);
    CHECK(backend_door_remedy(spaced) ==
          "apogee config rename-backend \"a b\" <new name> && apogee config set-backend <new name> "
          "--model-path <an existing .gguf>");
    CHECK(backend_door_said(spaced) ==
          "rename it first: apogee config rename-backend \"a b\" <new name>, then apogee config "
          "set-backend <new name> --model-path <an existing .gguf>");
}

TEST_CASE("a sentence that sets one command apart says the rename and the door its own way",
          "[contracts][backend-door][M50]") {
    using apogee::harness::backend_door_with;
    CHECK(
        backend_door_with("remove the backend itself", backend_edit_door("gone", delete_backend)) ==
        "remove the backend itself with 'apogee config delete-backend gone'");
    CHECK(backend_door_with("remove the backend itself", backend_edit_door("-x", delete_backend)) ==
          "to remove the backend itself, rename it first: apogee config rename-backend -- -x <new "
          "name>, then apogee config delete-backend <new name>");
    // A sentence that sets its command apart with no quotes.
    CHECK(backend_door_with("remove it", backend_edit_door("gone", delete_backend), "") ==
          "remove it with apogee config delete-backend gone");
    CHECK(backend_door_with("remove it", backend_edit_door("a b", delete_backend), "") ==
          "to remove it, rename it first: apogee config rename-backend \"a b\" <new name>, then "
          "apogee config delete-backend <new name>");
}

TEST_CASE("the two editors' doors are built once, the options after the name",
          "[contracts][backend-door][M50]") {
    using apogee::harness::delete_backend_door;
    using apogee::harness::option;
    using apogee::harness::set_backend_door;
    CHECK(backend_door_remedy(set_backend_door("local", {option("--adapters", "")})) ==
          "apogee config set-backend local --adapters \"\"");
    CHECK(
        backend_door_remedy(set_backend_door("-x", {option("--draft", ""), option("--x", "y")})) ==
        "apogee config rename-backend -- -x <new name> && apogee config set-backend <new name> "
        "--draft \"\" --x y");
    CHECK(backend_door_said(delete_backend_door("gone")) == "apogee config delete-backend gone");
    CHECK(backend_door_said(delete_backend_door("a;b")) ==
          "rename it first: apogee config rename-backend \"a;b\" <new name>, then apogee config "
          "delete-backend <new name>");
}
