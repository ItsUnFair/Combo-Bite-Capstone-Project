// combobite-engine: the C++ half of ComboBite.
//
// For now (task 1.1) it supports:
//   combobite-engine --version
//   combobite-engine summary <chain file>   (prints what a chain file contains)
// The JSON commands the Python front end uses, the loader with error
// messages, --check-data and --test-optimizer come in later tasks.
#include <fstream>
#include <iostream>
#include <string>

#include "combobite/menu.hpp"

namespace {

void print_usage() {
    std::cout << "usage:\n"
                 "  combobite-engine --version\n"
                 "  combobite-engine summary <chain file>\n";
}

int summary(const std::string& path) {
    std::ifstream in(path);
    if (!in) {
        std::cerr << "error: cannot open " << path << '\n';
        return 1;
    }
    try {
        const auto chain = nlohmann::json::parse(in).get<combobite::Chain>();
        std::cout << chain.name << " (" << chain.id << ")\n"
                  << "  prices checked " << chain.prices_checked.date
                  << " at " << chain.prices_checked.store << '\n'
                  << "  " << chain.items.size() << " items, "
                  << chain.combos.size() << " combos, "
                  << chain.bundles.size() << " bundles, "
                  << chain.add_ons.size() << " add-ons\n";
    } catch (const nlohmann::json::exception& e) {
        std::cerr << "error: " << path << ": " << e.what() << '\n';
        return 1;
    }
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc >= 2 && std::string(argv[1]) == "--version") {
        std::cout << "combobite-engine " << COMBOBITE_VERSION << '\n';
        return 0;
    }
    if (argc == 3 && std::string(argv[1]) == "summary") return summary(argv[2]);
    print_usage();
    return argc == 1 ? 0 : 2;
}
