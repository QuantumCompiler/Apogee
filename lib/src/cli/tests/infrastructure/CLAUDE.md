# The Infrastructure tests

**Mirror.** `tests/infrastructure/<module>/` tests `source/infrastructure/<module>/` ([ADR 0004](../../../../documentation/adrs/cli/tests-mirror-architecture.md)); a new module brings its test directory in the same change. `version/` has no directory: the root `smoke_test.cpp` covers it.

**Compiles as** `apogee_tests_infrastructure`, an OBJECT library linking the Infrastructure modules only: a test that includes a header from above this layer does not compile, and the link policy refuses the library a link above it. A behavior spanning layers is tested in the highest layer it touches -- nothing here drives a contract, a backend or the Harness.

**Support** (`tests/support/`, each file with the lowest layer whose headers it includes): `env_guard`, `file_time`, `terminal_model`, `gguf_builder`, `fake_child`, `fake_ffmpeg`, `embedding_fixtures`.

**Conventions:** hermetic -- no network, no models, nothing written outside the test's own temporary directory, named with a random draw because ctest runs cases as parallel processes, and never the real home directory (guard `HOME`). A test name never starts with a dash or holds a double quote (`cli.test_names`). One `apogee_tests` binary runs every layer's suites.

**Depth:** [ADR 0004](../../../../documentation/adrs/cli/tests-mirror-architecture.md) · [ADR 0001](../../../../documentation/adrs/cli/layer-enforcement.md) · [DEVELOPER.md](../../../../documentation/assistant/DEVELOPER.md) (the test table) · [CLAUDE.md](../../../../documentation/assistant/CLAUDE.md).
