# sandbox.sh -- a run's own scratch directory, for the executable tier's
# scripts (M17, 2026-10-20). Sourced, never run:
#
#   . "$(dirname "$0")/sandbox.sh"
#   WORK_DIR="$(sandbox_dir "$WORK_DIR")"
#
# ctest hands each script a fixed path in the build tree. Taken as it was, two
# runs of the suite in one tree -- `make test` started while a gate ran --
# shared it: one run's setup wiped the other's home mid-test, a config the
# other had made "already exists", the other's driver "outlived its apogee",
# and thirteen lifecycle tests failed with nothing wrong in the product. So a
# run takes a fresh directory beside that path and removes it whole when it
# ends (the script's EXIT trap). A directory beside it that a killed run left
# -- older than any test may live: the longest ctest timeout is 15 minutes --
# is swept on the way in. tests/scripts/cmake/sandbox.cmake is the same rule
# for the CMake scripts.
sandbox_dir() {
    _sandbox_parent="$(dirname "$1")"
    mkdir -p "$_sandbox_parent"
    find "$_sandbox_parent" -maxdepth 1 -type d -name "$(basename "$1").*" -mmin +60 \
        -exec rm -rf {} + 2>/dev/null || true
    mktemp -d "$1.XXXXXX"
}
