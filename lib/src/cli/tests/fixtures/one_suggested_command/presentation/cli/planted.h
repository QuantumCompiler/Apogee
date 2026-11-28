// cli.one_suggested_command_planted runs the check over this folder: each way
// a printed command is joined by hand, planted once, beside the house voice it
// must let through. Never compiled: a header, so `make lint` (clang-tidy over
// .cpp files) never parses it.
#include <iostream>
#include <string>

std::string planted(const std::string& name, const std::string& path, bool fresh) {
    std::cerr << "apogee graph: no graph built for '" << name << "'\n";
    std::cerr << "apogee " << name << ": the house voice, its verb joined\n";
    std::string chain = "no graph built -- run: apogee graph build " + name;
    std::string chosen = fresh ? "apogee graph build " + name : std::string{};
    std::cout << "Build it:\n  apogee graph build " << name << "\n";
    std::string mode = "chmod 700 '" + path + "'";
    std::string spliced = harness::printed_program() + " graph build " + name;
    std::string bare = "resume it with: apogee " + name + " --resume";
    return chain + chosen + mode + spliced + bare;
}
