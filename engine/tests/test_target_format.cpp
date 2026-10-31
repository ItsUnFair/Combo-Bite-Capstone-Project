// Task 2.1 test: the shared target examples (tests/target_examples.json)
// read and write the same way in C++ as in Python.
//
// Usage: target_tests <path to target_examples.json>
#include <fstream>
#include <iostream>
#include <string>

#include "combobite/target.hpp"

using namespace combobite;

int main(int argc, char** argv) {
    if (argc != 2) {
        std::cerr << "usage: target_tests <target_examples.json>\n";
        return 2;
    }
    std::ifstream in(argv[1]);
    if (!in) {
        std::cerr << "cannot open " << argv[1] << '\n';
        return 2;
    }
    const auto examples = nlohmann::json::parse(in);
    int failures = 0;

    for (const auto& example : examples.at("valid")) {
        const std::string name = example.at("name");
        const auto& expected = example.contains("canonical") ? example.at("canonical") : example.at("target");
        try {
            const Target target = example.at("target").get<Target>();
            const nlohmann::json written = target;
            if (written != expected) {
                ++failures;
                std::cerr << "FAIL (valid): " << name << "\n  expected " << expected.dump()
                          << "\n  got      " << written.dump() << '\n';
            }
        } catch (const TargetFormatError& e) {
            ++failures;
            std::cerr << "FAIL (valid): " << name << " was rejected: " << e.what() << '\n';
        }
    }

    for (const auto& example : examples.at("invalid")) {
        const std::string name = example.at("name");
        try {
            (void)example.at("target").get<Target>();
            ++failures;
            std::cerr << "FAIL (invalid): " << name << " was accepted\n";
        } catch (const TargetFormatError&) {
            // expected
        }
    }

    // The error message names the field.
    try {
        (void)nlohmann::json::parse(R"({"format_version":1,"items":[{"item":"a"},{"item":"b","quantity":0}]})")
            .get<Target>();
        ++failures;
        std::cerr << "FAIL: zero quantity accepted\n";
    } catch (const TargetFormatError& e) {
        if (std::string(e.what()).find("items[1].quantity") == std::string::npos) {
            ++failures;
            std::cerr << "FAIL: message doesn't name the field: " << e.what() << '\n';
        }
    }

    if (failures == 0) std::cout << "target format: all checks passed\n";
    return failures == 0 ? 0 : 1;
}
