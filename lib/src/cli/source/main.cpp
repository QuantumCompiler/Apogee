// The `apogee` executable.
//
// Deliberately almost empty. Every capability lives in apogee_core, which the
// tests and (later) the HTTP server link instead of this target -- that split
// is what makes surface parity structural rather than something a reviewer has
// to notice. cmake/ApogeeLinkPolicy.cmake fails the build if anything ever
// starts linking this executable. Resist the urge to put a helper here.

#include <exception>
#include <iostream>

#include "commands/root.h"

int main(int argc, char** argv) {
    try {
        apogee::commands::RootCommand root;
        return root.run(argc, argv);
    } catch (const std::exception& e) {
        std::cerr << "apogee: " << e.what() << "\n";
        return 1;
    }
}
