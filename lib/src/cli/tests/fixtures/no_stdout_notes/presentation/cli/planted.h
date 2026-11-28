// cli.no_stdout_notes_planted runs the check over this folder: two notes
// written on stdout, one per voice it reads. Never compiled: a header, so
// `make lint` (clang-tidy over .cpp files) never parses it.
#include <iostream>

void planted() {
    std::cout << "[graph] x\n";
    std::cout << "warning: x\n";
    std::cerr << "[graph] progress, on stderr, is fine\n";
}
