# The Presentation tests

**Mirror.** `tests/presentation/<module>/` tests `source/presentation/<module>/` ([ADR 0004](../../../../documentation/adrs/cli/tests-mirror-architecture.md)); a new module brings its test directory in the same change.

**Compiles as** `apogee_tests_presentation`, an OBJECT library linking every module: a test that includes a header from above this layer does not compile, and the link policy refuses the library a link above it. A behavior spanning layers is tested in the highest layer it touches -- the top, so a surface's test may drive the whole stack.

**Support** (`tests/support/`, each file with the lowest layer whose headers it includes): `cli_home`, `fake_command`, and those of the layers below.

**Conventions:** hermetic -- no network, no models, nothing written outside the test's own temporary directory, named with a random draw because ctest runs cases as parallel processes, and never the real home directory (guard `HOME`; `APOGEE_HOME` is a directory of the run's own, `support/hermetic_home.cpp` -- a test that needs a home still makes its own, and one that needs the variable unset says so with `EnvUnsetGuard`) or the terminal the suite was started from (stdin is empty for the run, `support/hermetic_stdin.cpp`; a test that needs input feeds std::cin its own buffer). A test name never starts with a dash or holds a double quote (`cli.test_names`). One `apogee_tests` binary runs every layer's suites.

**Depth:** [ADR 0004](../../../../documentation/adrs/cli/tests-mirror-architecture.md) · [ADR 0001](../../../../documentation/adrs/cli/layer-enforcement.md) · [DEVELOPER.md](../../../../documentation/assistant/DEVELOPER.md) (the test table) · [CLAUDE.md](../../../../documentation/assistant/CLAUDE.md).
