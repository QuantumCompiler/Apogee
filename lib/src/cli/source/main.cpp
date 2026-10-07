// The `apogee` executable.
//
// Deliberately almost empty. Every capability lives in apogee_core, which the
// tests and (later) the HTTP server link instead of this target -- that split
// is what makes surface parity structural rather than something a reviewer has
// to notice. cmake/ApogeeLinkPolicy.cmake fails the build if anything ever
// starts linking this executable. Resist the urge to put a helper here.

#include <exception>
#include <iostream>
#include <optional>

#include "cli/root.h"
#include "contracts/paths.h"

// The install channel this executable was built for (M10): stamped by CMake
// from -DAPOGEE_CHANNEL (`make install MODE=<channel>`), and held to the three
// names here, at compile time. It is the executable's identity, not a helper:
// what lets an installed binary know its own root before it reads any file.
#if !defined(APOGEE_CHANNEL)
#error "APOGEE_CHANNEL is stamped by source/CMakeLists.txt (apogee_channel_executable)"
#endif
static_assert(apogee::harness::parse_channel(APOGEE_CHANNEL).has_value(),
              "APOGEE_CHANNEL must be release, dev or test");
constexpr apogee::harness::Channel kChannel =
    apogee::harness::parse_channel(APOGEE_CHANNEL).value_or(apogee::harness::Channel::Release);

int main(int argc, char** argv) {
    apogee::harness::set_baked_channel(kChannel);
    try {
        apogee::commands::RootCommand root;
        return root.run(argc, argv);
    } catch (const std::exception& e) {
        std::cerr << "apogee: " << e.what() << "\n";
        return 1;
    }
}
