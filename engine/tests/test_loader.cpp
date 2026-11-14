// Tasks 6.3 and 6.4 tests: one broken input per loader rule and data check.
//
// Each case starts from a small valid chain, breaks one thing, and checks
// that the right message appears for the right entry, and that the chain
// loads (warnings) or doesn't (errors).
//
// Usage: loader_tests <path to data/chains>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <string>
#include <vector>

#include "combobite/checks.hpp"
#include "combobite/dates.hpp"
#include "combobite/loader.hpp"

using namespace combobite;
using nlohmann::json;
namespace fs = std::filesystem;

namespace {

const Date kToday{2026, 11, 6};
int failures = 0;

void check(bool ok, const std::string& what) {
    if (!ok) {
        ++failures;
        std::cerr << "FAIL: " << what << '\n';
    }
}

json base_chain() {
    return json::parse(R"({
      "format_version": 1,
      "chain": { "id": "test", "name": "Test Chain", "aliases": ["TC"] },
      "prices_checked": { "date": "2026-11-01", "store": "Test Store" },
      "categories": ["burger", "side", "drink"],
      "items": [
        { "id": "burger", "name": "Burger", "category": "burger",
          "sizes": [ { "size": "regular", "price_cents": 500, "calories": 500 } ] },
        { "id": "fries", "name": "Fries", "category": "side",
          "sizes": [ { "size": "small", "price_cents": 200, "calories": 200 },
                     { "size": "large", "price_cents": 300, "calories": 300 } ] },
        { "id": "cola", "name": "Cola", "category": "drink",
          "sizes": [ { "size": "small", "price_cents": 150, "calories": 150 },
                     { "size": "large", "price_cents": 200, "calories": 250 } ] }
      ],
      "add_ons": [
        { "id": "add_cheese", "name": "Add Cheese", "price_cents": 50, "calories": 50,
          "applies_to_categories": ["burger"] }
      ],
      "combos": [
        { "id": "burger_combo", "name": "Burger Combo", "price_cents": 700, "base_size": "small",
          "slots": [
            { "role": "main",  "choices": [ { "item": "burger" } ] },
            { "role": "side",  "choices": [ { "item": "fries", "size": "small" } ] },
            { "role": "drink", "choices": [ { "item": "cola", "size": "small" } ] } ],
          "upsizes": [ { "name": "large", "extra_cents": 80, "resize": { "side": "large", "drink": "large" } } ] }
      ],
      "bundles": [
        { "id": "two_for_eight", "name": "2 for $8", "price_cents": 800,
          "slots": [ { "role": "pick", "count": 2, "choices": [ { "item": "burger" } ] } ] }
      ]
    })");
}

struct Case {
    std::string name;
    std::function<void(json&)> mutate;  // used when text is empty
    std::string text;                    // raw file text, for syntax errors
    Severity severity;
    std::string entry;    // expected entry, or "" to skip that check
    std::string message;  // expected part of the message
    bool loads;
};

void run_case(const Case& c) {
    std::string text = c.text;
    if (text.empty()) {
        json chain = base_chain();
        c.mutate(chain);
        text = chain.dump(2);
    }
    const ChainReport report = load_chain_text(text, "case.json", kToday);
    bool found = false;
    for (const auto& issue : report.issues)
        if (issue.severity == c.severity && issue.message.find(c.message) != std::string::npos &&
            (c.entry.empty() || issue.entry == c.entry))
            found = true;
    check(found, c.name + ": expected " + (c.severity == Severity::Error ? "error" : "warning") + " at '" +
                     c.entry + "' containing \"" + c.message + "\"");
    check(report.loaded() == c.loads, c.name + (c.loads ? ": should still load" : ": should not load"));
    if (!found || report.loaded() != c.loads)
        for (const auto& issue : report.issues) std::cerr << "    got: " << issue.entry << ": " << issue.message << '\n';
}

std::vector<Case> cases() {
    const auto E = Severity::Error;
    const auto W = Severity::Warning;
    auto none = [](json&) {};
    return {
        // ---- 6.3: file can't be read, shape problems ----
        {"syntax error (missing comma)", none,
         "{\n  \"format_version\": 1\n  \"chain\": {}\n}", E, "file", "line 3", false},
        {"empty file", none, " ", E, "file", "line 1", false},
        {"top level is a list", [](json& j) { j = json::array(); }, "", E, "file", "must be a JSON object", false},
        {"price written in dollars", [](json& j) { j["items"][0]["sizes"][0]["price_cents"] = 4.99; }, "", E,
         "item 'burger', size 'regular'", "write 4.99 as 499", false},
        {"price written as text", [](json& j) { j["items"][0]["sizes"][0]["price_cents"] = "$5"; }, "", E,
         "item 'burger', size 'regular'", "must be a whole number of cents", false},
        {"missing price", [](json& j) { j["items"][0]["sizes"][0].erase("price_cents"); }, "", E,
         "item 'burger', size 'regular'", "has no price", false},
        {"missing item name", [](json& j) { j["items"][1].erase("name"); }, "", E, "item 'fries'",
         "is missing \"name\"", false},
        {"sizes not a list", [](json& j) { j["items"][1]["sizes"] = "small"; }, "", E, "item 'fries'",
         "\"sizes\" must be a list", false},
        {"item without an id", [](json& j) { j["items"][2].erase("id"); }, "", E, "items[2]", "is missing \"id\"",
         false},
        {"missing prices_checked", [](json& j) { j.erase("prices_checked"); }, "", E, "file",
         "is missing \"prices_checked\"", false},
        {"calories as text", [](json& j) { j["items"][0]["sizes"][0]["calories"] = "500"; }, "", E,
         "item 'burger', size 'regular'", "\"calories\" must be a whole number or null", false},

        // ---- 6.4 errors ----
        {"zero price", [](json& j) { j["items"][0]["sizes"][0]["price_cents"] = 0; }, "", E,
         "item 'burger', size 'regular'", "has a price of $0.00", false},
        {"negative price", [](json& j) { j["items"][0]["sizes"][0]["price_cents"] = -100; }, "", E,
         "item 'burger', size 'regular'", "negative price, -$1.00", false},
        {"combo uses an undefined item", [](json& j) { j["combos"][0]["slots"][0]["choices"][0]["item"] = "burgr"; },
         "", E, "combo 'burger_combo', slot 'main'", "uses item 'burgr', which this file doesn't define", false},
        {"duplicate item id", [](json& j) { j["items"][2]["id"] = "fries"; }, "", E, "item 'fries'",
         "is already used by item 'fries'", false},
        {"duplicate item name", [](json& j) { j["items"][2]["name"] = "fries"; }, "", E, "item 'cola'",
         "same name", false},
        {"item category not listed", [](json& j) { j["items"][0]["category"] = "sandwich"; }, "", E,
         "item 'burger'", "category 'sandwich', which isn't in \"categories\"", false},
        {"combo size not offered", [](json& j) { j["combos"][0]["slots"][1]["choices"][0]["size"] = "medium"; },
         "", E, "combo 'burger_combo', slot 'side'", "only comes in small, large", false},
        {"combo size left out", [](json& j) { j["combos"][0]["slots"][1]["choices"][0].erase("size"); }, "", E,
         "combo 'burger_combo', slot 'side'", "must say which size of 'fries'", false},
        {"slot with no choices", [](json& j) { j["bundles"][0]["slots"][0]["choices"] = json::array(); }, "", E,
         "bundle 'two_for_eight', slot 'pick'", "has no choices", false},
        {"slot count of zero", [](json& j) { j["bundles"][0]["slots"][0]["count"] = 0; }, "", E,
         "bundle 'two_for_eight', slot 'pick'", "\"count\" must be 1 or more", false},
        {"upsize resizes a missing slot", [](json& j) { j["combos"][0]["upsizes"][0]["resize"]["dessert"] = "large"; },
         "", E, "combo 'burger_combo', upsize 'large'", "no slot with that role", false},
        {"upsize to a size nothing has", [](json& j) { j["combos"][0]["upsizes"][0]["resize"]["side"] = "huge"; },
         "", E, "combo 'burger_combo', upsize 'large'", "nothing in that slot comes in 'huge'", false},
        {"bad prices_checked date", [](json& j) { j["prices_checked"]["date"] = "11/01/2026"; }, "", E,
         "prices_checked", "isn't a real date", false},
        {"impossible date", [](json& j) { j["prices_checked"]["date"] = "2026-02-30"; }, "", E, "prices_checked",
         "isn't a real date", false},
        {"wrong format_version", [](json& j) { j["format_version"] = 2; }, "", E, "file", "format_version 2",
         false},
        {"add-on for an undefined item", [](json& j) { j["add_ons"][0]["applies_to_items"] = {"shake"}; }, "", E,
         "add-on 'add_cheese'", "item 'shake', which this file doesn't define", false},
        {"combo with a zero price", [](json& j) { j["combos"][0]["price_cents"] = 0; }, "", E,
         "combo 'burger_combo'", "has a price of $0.00", false},

        // ---- 6.4 warnings ----
        {"no calories for one size", [](json& j) { j["items"][2]["sizes"][0]["calories"] = nullptr; }, "", W,
         "item 'cola'", "has no calorie data for small", true},
        {"no calories at all", [](json& j) { j["items"][0]["sizes"][0].erase("calories"); }, "", W,
         "item 'burger'", "has no calorie data;", true},
        {"combo costs more than its items", [](json& j) { j["combos"][0]["price_cents"] = 900; }, "", W,
         "combo 'burger_combo'", "more than its items bought separately ($8.50)", true},
        {"bundle costs the same as its items", [](json& j) { j["bundles"][0]["price_cents"] = 1000; }, "", W,
         "bundle 'two_for_eight'", "costs the same as its items bought separately ($10.00)", true},
        {"prices older than 90 days", [](json& j) { j["prices_checked"]["date"] = "2026-07-01"; }, "", W,
         "prices_checked", "128 days ago", true},
        {"date in the future", [](json& j) { j["prices_checked"]["date"] = "2026-12-25"; }, "", W,
         "prices_checked", "is in the future", true},
        {"unknown field (typo)", [](json& j) { j["combos"][0]["upsize"] = json::array(); }, "", W,
         "combo 'burger_combo'", "unknown field \"upsize\"", true},
        {"add-on applies to nothing", [](json& j) { j["add_ons"][0].erase("applies_to_categories"); }, "", W,
         "add-on 'add_cheese'", "applies to nothing", true},
    };
}

void test_base_chain_is_clean() {
    const ChainReport report = load_chain_text(base_chain().dump(), "base.json", kToday);
    check(report.loaded(), "base chain loads");
    check(report.issues.empty(), "base chain has no errors or warnings");
    for (const auto& issue : report.issues) std::cerr << "    got: " << issue.entry << ": " << issue.message << '\n';
}

void test_removed_item_lists_every_deal() {
    json chain = base_chain();
    chain["items"].erase(0);  // the burger is no longer sold
    chain["add_ons"][0]["applies_to_categories"] = {"side"};
    const ChainReport report = load_chain_text(chain.dump(), "removed.json", kToday);
    int mentions = 0;
    for (const auto& issue : report.issues)
        if (issue.message.find("uses item 'burger'") != std::string::npos) ++mentions;
    check(mentions == 2, "removing an item lists both the combo and the bundle that still use it");
    check(!report.loaded(), "removing an item that deals still use keeps the chain from loading");
}

void test_all_problems_reported_at_once() {
    json chain = base_chain();
    chain["items"][0]["sizes"][0]["price_cents"] = 4.99;
    chain["items"][1].erase("name");
    const ChainReport report = load_chain_text(chain.dump(), "two.json", kToday);
    check(report.count(Severity::Error) == 2, "two shape problems give two errors, not just the first");
}

void write(const fs::path& path, const std::string& text) {
    std::ofstream(path) << text;
}

void test_data_folder() {
    const fs::path dir = fs::temp_directory_path() / "combobite_loader_test";
    fs::remove_all(dir);
    fs::create_directories(dir);

    DataFolderReport empty = load_data_folder(dir, kToday);
    check(empty.fatal.find("No chain files") != std::string::npos, "empty folder is reported");

    write(dir / "a_good.json", base_chain().dump(2));
    write(dir / "b_broken.json", "{ \"format_version\": 1,, }");
    json copy = base_chain();
    copy["chain"]["name"] = "Copy";
    write(dir / "c_same_id.json", copy.dump(2));
    write(dir / "notes.md", "not a chain file");

    const DataFolderReport report = load_data_folder(dir, kToday);
    check(report.fatal.empty(), "folder with chain files has no fatal error");
    check(report.files.size() == 3, "only .json files are read");
    check(report.loaded_chains().size() == 1, "only the good file loads");
    if (report.files.size() == 3) {
        check(report.files[0].loaded(), "a_good.json loads");
        check(!report.files[1].loaded(), "b_broken.json doesn't load");
        check(!report.files[2].loaded() && !report.files[2].issues.empty() &&
                  report.files[2].issues.back().message.find("already used by a_good.json") != std::string::npos,
              "second file with the same chain id doesn't load and names the first");
    }
    fs::remove_all(dir);

    const DataFolderReport missing = load_data_folder(dir / "nope", kToday);
    check(missing.fatal.find("wasn't found") != std::string::npos && missing.fatal.find("nope") != std::string::npos,
          "missing folder is reported with the path it looked in");
}

void test_real_chains(const fs::path& folder) {
    const DataFolderReport report = load_data_folder(folder, kToday);
    check(report.fatal.empty(), "real data folder is found");
    check(report.loaded_chains().size() == report.files.size() && report.files.size() == 4,
          "all four real chain files load");
    check(report.count(Severity::Error) == 0, "real chain files have no errors");
    for (const auto& file : report.files)
        for (const auto& issue : file.issues)
            if (issue.severity == Severity::Error)
                std::cerr << "    " << file.file << ": " << issue.entry << ": " << issue.message << '\n';
}

void test_dates_and_money() {
    check(parse_date("2024-02-29").has_value(), "leap day 2024 is a real date");
    check(!parse_date("2026-02-29").has_value(), "2026-02-29 isn't a real date");
    check(!parse_date("2026-1-05").has_value(), "a date must be written YYYY-MM-DD");
    check(days_between(Date{2026, 10, 25}, Date{2026, 11, 6}) == 12, "days between two dates");
    check(days_between(Date{2025, 12, 31}, Date{2026, 1, 1}) == 1, "days across a year boundary");
    check(format_date(Date{2026, 3, 7}) == "2026-03-07", "format a date");
    check(format_money(449) == "$4.49" && format_money(5) == "$0.05" && format_money(-50) == "-$0.50",
          "format money");
}

}  // namespace

int main(int argc, char** argv) {
    if (argc != 2) {
        std::cerr << "usage: loader_tests <data/chains folder>\n";
        return 2;
    }
    test_base_chain_is_clean();
    const auto all = cases();
    for (const auto& c : all) run_case(c);
    test_removed_item_lists_every_deal();
    test_all_problems_reported_at_once();
    test_data_folder();
    test_real_chains(argv[1]);
    test_dates_and_money();

    if (failures == 0) std::cout << "loader and checks: all " << all.size() << " cases and other checks passed\n";
    return failures == 0 ? 0 : 1;
}
