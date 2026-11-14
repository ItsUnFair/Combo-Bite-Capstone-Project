// combobite-engine: the C++ half of ComboBite.
//
//   combobite-engine --version
//   combobite-engine check-data [--data <folder>] [--today YYYY-MM-DD]
//   combobite-engine summary <chain file>
//
// The Python front end runs `combobite --check-data`, which calls check-data.
// The JSON commands the front end will use for searching come in later tasks.
#include <iostream>
#include <string>
#include <vector>

#include "combobite/checks.hpp"
#include "combobite/dates.hpp"
#include "combobite/loader.hpp"

using namespace combobite;

namespace {

constexpr const char* kDefaultDataFolder = "data/chains";

void print_usage(std::ostream& out) {
    out << "usage:\n"
           "  combobite-engine --version\n"
           "  combobite-engine check-data [--data <folder>] [--today YYYY-MM-DD]\n"
           "  combobite-engine summary <chain file>\n";
}

std::string plural(int n, const char* word) {
    return std::to_string(n) + " " + word + (n == 1 ? "" : "s");
}

void print_issues(const ChainReport& report) {
    // Errors first, then warnings, each in file order.
    for (const Severity severity : {Severity::Error, Severity::Warning})
        for (const auto& issue : report.issues)
            if (issue.severity == severity)
                std::cout << (severity == Severity::Error ? "  error    " : "  warning  ") << issue.entry << ": "
                          << issue.message << '\n';
}

// Exit codes: 0 = no errors (warnings are fine), 1 = errors, 2 = nothing to check.
int check_data(const std::string& folder, const Date& today) {
    const DataFolderReport report = load_data_folder(folder, today);
    if (!report.fatal.empty()) {
        std::cout << report.fatal << '\n';
        return 2;
    }

    std::cout << "Checking chain files in " << folder << " (today is " << format_date(today) << ")\n\n";
    int not_loaded = 0;
    for (const auto& file : report.files) {
        const int errors = file.count(Severity::Error);
        const int warnings = file.count(Severity::Warning);
        std::cout << file.file;
        if (!file.chain_name.empty()) std::cout << " (" << file.chain_name << ")";
        if (file.loaded()) {
            std::cout << ": loaded";
            if (warnings) std::cout << ", " << plural(warnings, "warning");
        } else {
            ++not_loaded;
            std::cout << ": NOT LOADED, " << plural(errors, "error");
            if (warnings) std::cout << ", " << plural(warnings, "warning");
        }
        std::cout << '\n';
        print_issues(file);
    }

    const int files = static_cast<int>(report.files.size());
    std::cout << '\n'
              << plural(files, "chain file") << ": " << (files - not_loaded) << " loaded, " << not_loaded
              << " not loaded. " << plural(report.count(Severity::Error), "error") << ", "
              << plural(report.count(Severity::Warning), "warning") << ".\n";
    if (not_loaded == files) std::cout << "No chain loaded, so ComboBite can't start until these are fixed.\n";
    return report.count(Severity::Error) > 0 ? 1 : 0;
}

int summary(const std::string& path) {
    const ChainReport report = load_chain_file(path, today_local());
    if (!report.loaded()) {
        std::cout << report.file << ": NOT LOADED\n";
        print_issues(report);
        return 1;
    }
    const Chain& chain = *report.chain;
    std::cout << chain.name << " (" << chain.id << ")\n"
              << "  prices checked " << chain.prices_checked.date << " at " << chain.prices_checked.store << '\n'
              << "  " << chain.items.size() << " items, " << chain.combos.size() << " combos, "
              << chain.bundles.size() << " bundles, " << chain.add_ons.size() << " add-ons\n";
    print_issues(report);
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    const std::vector<std::string> args(argv + 1, argv + argc);
    if (args.empty()) {
        print_usage(std::cout);
        return 0;
    }
    if (args[0] == "--version") {
        std::cout << "combobite-engine " << COMBOBITE_VERSION << '\n';
        return 0;
    }
    if (args[0] == "summary" && args.size() == 2) return summary(args[1]);
    if (args[0] == "check-data") {
        std::string folder = kDefaultDataFolder;
        Date today = today_local();
        for (std::size_t i = 1; i < args.size(); ++i) {
            if (args[i] == "--data" && i + 1 < args.size()) {
                folder = args[++i];
            } else if (args[i] == "--today" && i + 1 < args.size()) {
                const auto date = parse_date(args[++i]);
                if (!date) {
                    std::cerr << "error: --today needs a date written as YYYY-MM-DD\n";
                    return 2;
                }
                today = *date;
            } else {
                std::cerr << "error: unknown option '" << args[i] << "'\n";
                print_usage(std::cerr);
                return 2;
            }
        }
        return check_data(folder, today);
    }
    print_usage(std::cerr);
    return 2;
}
