#include "commands/registry.h"

#include <CLI/CLI.hpp>
#include <catch2/catch_test_macros.hpp>

#include <memory>
#include <stdexcept>
#include <string_view>
#include <vector>

#include "support/fake_command.h"

using apogee::commands::CommandRegistry;
using apogee::commands::RootContext;
using apogee::testing::FakeCommand;

TEST_CASE("a new registry is empty", "[commands][registry]") {
    const CommandRegistry registry;

    REQUIRE(registry.empty());
    REQUIRE(registry.names().empty());
    REQUIRE(registry.find("version") == nullptr);
}

TEST_CASE("added commands are findable by name", "[commands][registry]") {
    CommandRegistry registry;
    registry.add(std::make_unique<FakeCommand>("alpha", "the first"));
    registry.add(std::make_unique<FakeCommand>("beta", "the second"));

    REQUIRE(registry.size() == 2);
    REQUIRE_FALSE(registry.empty());

    const auto* alpha = registry.find("alpha");
    REQUIRE(alpha != nullptr);
    REQUIRE(alpha->summary() == "the first");

    REQUIRE(registry.find("gamma") == nullptr);
}

TEST_CASE("names come back in registration order", "[commands][registry]") {
    CommandRegistry registry;
    registry.add(std::make_unique<FakeCommand>("first", ""));
    registry.add(std::make_unique<FakeCommand>("second", ""));
    registry.add(std::make_unique<FakeCommand>("third", ""));

    const std::vector<std::string_view> expected{"first", "second", "third"};
    REQUIRE(registry.names() == expected);
}

TEST_CASE("a duplicate command name is rejected, not silently shadowed", "[commands][registry]") {
    CommandRegistry registry;
    registry.add(std::make_unique<FakeCommand>("dupe", "original"));

    REQUIRE_THROWS_AS(registry.add(std::make_unique<FakeCommand>("dupe", "impostor")),
                      std::invalid_argument);

    // The original survives the rejection.
    REQUIRE(registry.size() == 1);
    REQUIRE(registry.find("dupe")->summary() == "original");
}

TEST_CASE("a null command is rejected", "[commands][registry]") {
    CommandRegistry registry;

    REQUIRE_THROWS_AS(registry.add(nullptr), std::invalid_argument);
    REQUIRE(registry.empty());
}

TEST_CASE("bind_all attaches every registered command to the app", "[commands][registry]") {
    CommandRegistry registry;
    registry.add(std::make_unique<FakeCommand>("alpha", "the first"));
    registry.add(std::make_unique<FakeCommand>("beta", "the second"));

    CLI::App app{"test", "test"};
    const RootContext context;
    registry.bind_all(app, context);

    REQUIRE(app.get_subcommand("alpha") != nullptr);
    REQUIRE(app.get_subcommand("beta") != nullptr);
}

TEST_CASE("the default registry exposes the built-in command set", "[commands][registry]") {
    const CommandRegistry registry = apogee::commands::default_registry();

    REQUIRE_FALSE(registry.empty());

    const auto* version = registry.find("version");
    REQUIRE(version != nullptr);
    REQUIRE_FALSE(version->summary().empty());
}
