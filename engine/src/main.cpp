// combobite-engine: the C++ half of ComboBite.
//
//   combobite-engine --version
//   combobite-engine check-data [--data <folder>] [--today YYYY-MM-DD]
//   combobite-engine summary <chain file>
//   combobite-engine candidates <target file> [--data <folder>] [--show N] [--limit N]
//   combobite-engine candidates --case <id> [--show N] [--limit N]
//
// The Python front end runs `combobite --check-data`, which calls check-data.
// The JSON commands the front end will use for searching come in later tasks.
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#include "combobite/candidates.hpp"
#include "combobite/pricing.hpp"
#include "combobite/checks.hpp"
#include "combobite/dates.hpp"
#include "combobite/loader.hpp"

using namespace combobite;

namespace {

constexpr const char* kDefaultDataFolder = "data/chains";
constexpr const char* kCasesFile = "tests/optimizer_cases.json";

void print_usage(std::ostream& out) {
    out << "usage:\n"
           "  combobite-engine --version\n"
           "  combobite-engine check-data [--data <folder>] [--today YYYY-MM-DD]\n"
           "  combobite-engine summary <chain file>\n"
           "  combobite-engine candidates <target file> [--data <folder>] [--show N] [--limit N]\n"
           "  combobite-engine candidates --case <id> [--show N] [--limit N]\n";
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

// "1,170 cal", or "590+ cal" when some items have no calorie data.
std::string calories_text(const PricedOrder& order) {
    std::string n = std::to_string(order.calories);
    for (int i = static_cast<int>(n.size()) - 3; i > 0; i -= 3) n.insert(static_cast<std::size_t>(i), ",");
    return n + (order.calories_complete ? "" : "+") + " cal";
}

// Tasks 3.1 and 3.2: lists the candidate orders for a target with each
// one's price and calories. Budget and calorie limits aren't applied yet
// (task 3.3), so this shows everything the search will choose from.
// With case_id set, the target is that case from tests/optimizer_cases.json
// (task 8.1), and the sample chain is read from data/sample.
int candidates(const std::string& target_file, const std::string& case_id, std::string folder, long long show,
               long long limit) {
    Target target;
    const std::string source = case_id.empty() ? target_file : std::string(kCasesFile);
    try {
        std::ifstream in(source);
        if (!in) {
            std::cerr << "error: can't open " << source << " (run this from the project folder)\n";
            return 2;
        }
        const nlohmann::json j = nlohmann::json::parse(in);
        if (case_id.empty()) {
            target = j.get<Target>();
        } else {
            const nlohmann::json* found = nullptr;
            for (const auto& c : j.at("cases"))
                if (c.at("id") == case_id) found = &c;
            if (!found) {
                std::cerr << "error: no case '" << case_id << "' in " << source << '\n';
                return 2;
            }
            target = found->at("target").get<Target>();
            std::cout << "Case " << case_id << ": " << found->value("request", "") << '\n';
            if (target.chain == std::string("sample_burger")) folder = "data/sample";
        }
    } catch (const std::exception& e) {
        std::cerr << "error: " << source << ": " << e.what() << '\n';
        return 2;
    }
    if (!target.chain) {
        std::cerr << "error: the target doesn't name a chain\n";
        return 2;
    }

    const DataFolderReport data = load_data_folder(folder, today_local());
    const Chain* chain = nullptr;
    for (const Chain* c : data.loaded_chains())
        if (c->id == *target.chain) chain = c;
    if (!chain) {
        std::cerr << "error: no chain '" << *target.chain << "' loaded from " << folder << '\n';
        return 2;
    }

    const PurchaseList list = list_purchases(*chain, uses_calories(target));
    std::cout << chain->name << ": " << plural(wanted_units(target), "wanted item") << ", goal "
              << (target.goal == Goal::MostCalories ? "most calories" : "cheapest") << '\n';
    if (!list.left_out.empty()) {
        std::cout << "Left out (no calorie data): ";
        for (std::size_t i = 0; i < list.left_out.size(); ++i) std::cout << (i ? ", " : "") << list.left_out[i];
        std::cout << '\n';
    }

    if (wanted_units(target) > kItemCap) {
        std::cout << "More than " << kItemCap << " items wanted, so the search isn't run.\n";
        return 0;
    }

    // Show the first `show` candidates; stop counting at `limit` (0 = no limit).
    SearchHooks hooks;
    long long seen = 0;
    Cents lowest = -1;
    std::string lowest_text;
    hooks.visit = [&](const Candidate& c) {
        ++seen;
        const PricedOrder priced = price_order(c);
        if (lowest < 0 || priced.total_cents < lowest) {
            lowest = priced.total_cents;
            lowest_text = describe_candidate(c);
        }
        if (seen <= show)
            std::cout << "  " << seen << ". " << format_money(priced.total_cents) << "  " << calories_text(priced)
                      << "  " << describe_candidate(c) << '\n';
        return limit <= 0 || seen < limit;
    };
    std::cout << "Candidate orders" << (show > 0 ? " (first " + std::to_string(show) + ")" : "") << ":\n";
    const SearchStats result = find_candidates(*chain, target, list, hooks);
    if (result.candidates == 0) std::cout << "  none\n";
    std::cout << "\n"
              << list.purchases.size() << " things to order on the menu, " << result.things_to_choose_from
              << " tried for this target.\n"
              << (result.status == SearchStatus::Stopped ? "Stopped after " : "")
              << plural(static_cast<int>(result.candidates), "candidate order") << " ("
              << result.partial_orders << " orders looked at).\n";
    if (lowest >= 0 && result.status == SearchStatus::Done)
        std::cout << "Lowest total: " << format_money(lowest) << ", " << lowest_text << '\n'
                  << "(Budget and calorie limits aren't applied yet; that's task 3.3.)\n";
    if (result.status == SearchStatus::Stopped)
        std::cout << "Budget and calorie limits will narrow this search (tasks 3.3 and 3.4).\n";
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
    if (args[0] == "candidates" && args.size() >= 2) {
        std::string folder = kDefaultDataFolder, target_file, case_id;
        long long show = 10, limit = 100000;
        for (std::size_t i = 1; i < args.size(); ++i) {
            try {
                if (args[i] == "--data" && i + 1 < args.size()) folder = args[++i];
                else if (args[i] == "--case" && i + 1 < args.size()) case_id = args[++i];
                else if (i == 1 && args[i].rfind("--", 0) != 0) target_file = args[i];
                else if (args[i] == "--show" && i + 1 < args.size()) show = std::stoll(args[++i]);
                else if (args[i] == "--limit" && i + 1 < args.size()) limit = std::stoll(args[++i]);
                else throw std::invalid_argument(args[i]);
            } catch (const std::exception&) {
                std::cerr << "error: unknown option or bad number near '" << args[i] << "'\n";
                print_usage(std::cerr);
                return 2;
            }
        }
        if (target_file.empty() == case_id.empty()) {
            std::cerr << "error: give a target file or --case <id>\n";
            print_usage(std::cerr);
            return 2;
        }
        return candidates(target_file, case_id, folder, show, limit);
    }
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
