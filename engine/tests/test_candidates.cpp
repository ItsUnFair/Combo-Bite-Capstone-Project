// Task 3.1 tests: listing things to order and generating candidate orders.
//
// The sample chain's counts below were worked out by hand from
// data/sample/sample_chain.json. The last test runs every hand-calculated
// case in tests/optimizer_cases.json (task 8.1) and checks the expected
// order is one of the candidates, so the right answer is always there for
// tasks 3.2 and 3.3 to find.
//
// Usage: candidates_tests <project folder>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <iostream>
#include <map>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

#include "combobite/candidates.hpp"
#include "combobite/dates.hpp"
#include "combobite/loader.hpp"

using namespace combobite;
using nlohmann::json;
namespace fs = std::filesystem;

namespace {

int failures = 0;

void check(bool ok, const std::string& what) {
    if (!ok) {
        ++failures;
        std::cerr << "FAIL: " << what << '\n';
    }
}

Chain load(const fs::path& file) {
    ChainReport report = load_chain_file(file, Date{2026, 11, 21});
    if (!report.loaded()) throw std::runtime_error("couldn't load " + file.string());
    return *report.chain;
}

Target make_target(const json& j) { return j.get<Target>(); }

json wanted(const std::string& item, int quantity = 1, const json& size = nullptr,
            const json& add_ons = json::array()) {
    return {{"item", item}, {"quantity", quantity}, {"size", size}, {"add_ons", add_ons}};
}

json target_json(const std::string& chain, std::initializer_list<json> item_list,
                 const std::string& goal = "cheapest") {
    json items = json::array();
    for (const auto& i : item_list) items.push_back(i);
    return {{"format_version", 1}, {"chain", chain}, {"goal", goal}, {"max_price_cents", nullptr},
            {"min_calories", nullptr}, {"max_calories", nullptr}, {"items", items},
            {"unsupported", json::array()}};
}

int count_kind(const PurchaseList& list, PurchaseKind kind) {
    return static_cast<int>(std::count_if(list.purchases.begin(), list.purchases.end(),
                                          [&](const Purchase& p) { return p.kind == kind; }));
}

const Purchase* find_by_text(const PurchaseList& list, const std::string& text) {
    for (const auto& p : list.purchases)
        if (p.describe() == text) return &p;
    return nullptr;
}

// Order lines in a fixed order, so two orders can be compared.
json canonical(json order) {
    for (auto& line : order)
        if (line.contains("contents")) {
            std::vector<std::string> parts;
            for (const auto& c : line["contents"]) parts.push_back(c.dump());
            std::sort(parts.begin(), parts.end());
            line["contents"] = json::array();
            for (const auto& p : parts) line["contents"].push_back(json::parse(p));
        }
    std::vector<std::string> lines;
    for (const auto& line : order) lines.push_back(line.dump());
    std::sort(lines.begin(), lines.end());
    json out = json::array();
    for (const auto& l : lines) out.push_back(json::parse(l));
    return out;
}

std::vector<json> all_candidates(const Chain& chain, const Target& target, const PurchaseList& list,
                                 SearchStats* stats_out = nullptr) {
    std::vector<json> out;
    SearchHooks hooks;
    hooks.visit = [&](const Candidate& c) {
        out.push_back(canonical(candidate_to_json(c)));
        return true;
    };
    SearchStats stats = find_candidates(chain, target, list, hooks);
    if (stats_out) *stats_out = stats;
    return out;
}

// ---------------------------------------------------------------- tests ----

void test_sample_things_to_order(const Chain& sample) {
    const PurchaseList all = list_purchases(sample, false);
    // Singles: classic, cheese, double, chicken sandwich (1 each), nuggets 3,
    // fries 3, apple slices 1, salad 1, drink 3, cookie 1 = 16 sizes.
    check(count_kind(all, PurchaseKind::Single) == 16, "sample: 16 single item sizes");
    // Classic Burger Combo: 3 sides x (as it comes, large) = 6.
    // Chicken Combo: 2 mains x 2 sides x (as it comes, large) = 8.
    check(count_kind(all, PurchaseKind::Combo) == 14, "sample: 14 combo versions");
    // 2 for $6: 3 choices, pick 2 with repeats = 6. $5 Snack Box: 2 mains = 2.
    check(count_kind(all, PurchaseKind::Bundle) == 8, "sample: 8 bundle versions");
    check(all.purchases.size() == 38, "sample: 38 things to order in all");
    check(all.left_out.empty(), "sample: nothing left out when calories don't matter");

    const PurchaseList with_calories = list_purchases(sample, true);
    check(with_calories.purchases.size() == 37, "sample: the cookie is left out when calories matter");
    check(with_calories.left_out == std::vector<std::string>{"Chocolate Chip Cookie"}, "sample: left-out list names the cookie");
}

void test_upsizes(const Chain& sample) {
    const PurchaseList all = list_purchases(sample, false);
    check(find_by_text(all, "Classic Burger Combo (Classic Burger, medium Fries, medium Soft Drink)") != nullptr,
          "combo as it comes keeps medium sizes");
    check(find_by_text(all, "Classic Burger Combo, large (Classic Burger, large Fries, large Soft Drink)") != nullptr,
          "large combo makes the side and drink large");
    check(find_by_text(all, "Classic Burger Combo, large (Classic Burger, Apple Slices, large Soft Drink)") != nullptr,
          "apple slices have no large, so they stay regular in a large combo");
    check(find_by_text(all, "Chicken Combo, large (10 pc Chicken Nuggets, large Fries, large Soft Drink)") != nullptr,
          "the main isn't resized (only side and drink are named)");
}

void test_pick_two(const Chain& sample) {
    const PurchaseList all = list_purchases(sample, false);
    std::set<std::string> seen;
    for (const auto& p : all.purchases)
        if (p.kind == PurchaseKind::Bundle && p.bundle->id == "two_for_six") seen.insert(p.describe());
    check(seen.size() == 6, "2 for $6: 6 different pairs");
    check(seen.count("2 for $6 Mix & Match (Classic Burger, Classic Burger)") == 1, "2 for $6: the same choice twice");
    check(seen.count("2 for $6 Mix & Match (Crispy Chicken Sandwich, Classic Burger)") == 0, "2 for $6: each pair listed once");
}

void test_coverage(const Chain& sample) {
    const PurchaseList all = list_purchases(sample, false);
    const Purchase* burger = find_by_text(all, "Classic Burger");
    const Purchase* combo = find_by_text(all, "Classic Burger Combo (Classic Burger, medium Fries, medium Soft Drink)");
    const Purchase* sandwich = find_by_text(all, "Crispy Chicken Sandwich");
    const Purchase* cheese = find_by_text(all, "Cheeseburger");

    const Coverage two(sample, make_target(target_json("sample_burger", {wanted("classic_burger", 2)})));
    check(!two.covers({combo}), "quantity 2 isn't covered by one burger");
    check(two.covers({combo, burger}), "quantity 2 is covered by a combo's burger and a single one");

    const Coverage large_fries(sample, make_target(target_json("sample_burger", {wanted("fries", 1, "large")})));
    check(!large_fries.covers({combo}), "medium fries don't cover 'large fries'");

    json any_burger = {{"category", "burger"}, {"quantity", 1}, {"size", nullptr}, {"add_ons", {"add_bacon"}}};
    const Coverage bacon(sample, make_target(target_json("sample_burger", {any_burger})));
    check(bacon.covers({burger}), "a bacon burger is covered by a burger");
    check(!bacon.covers({sandwich}), "a chicken sandwich isn't in the burger category");

    std::vector<AddOnPlacement> placed;
    const Coverage combo_bacon(sample, make_target(target_json("sample_burger",
                                                               {wanted("classic_burger", 1, nullptr, {"add_bacon"})})));
    check(combo_bacon.covers({combo}, &placed) && placed.size() == 1 && placed[0].part == 0,
          "bacon goes on the combo's burger (part 0)");

    const Coverage triple(sample, make_target(target_json(
                                      "sample_burger", {wanted("cheeseburger", 1, nullptr,
                                                               {"add_cheese", "add_cheese", "add_cheese"})})));
    check(!triple.covers({cheese}), "cheese 3 times is over Add Cheese's limit of 2");

    bool threw = false;
    try {
        Coverage(sample, make_target(target_json("sample_burger", {wanted("classic_burger", 1, nullptr, {"add_gold"})})));
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    check(threw, "an add-on the chain doesn't have is an error");
}

void test_item_cap(const Chain& sample) {
    const Target eleven = make_target(target_json("sample_burger", {wanted("cheeseburger", 11)}));
    const PurchaseList all = list_purchases(sample, false);
    SearchStats stats;
    check(all_candidates(sample, eleven, all, &stats).empty() && stats.status == SearchStatus::TooManyItems,
          "11 wanted items: refused, nothing searched");

    // Budget-only target, only cheeseburgers allowed: 1 to 10 of them.
    const Target budget_only = make_target(target_json("sample_burger", {}, "most_calories"));
    const PurchaseList with_calories = list_purchases(sample, true);
    SearchHooks hooks;
    std::size_t biggest = 0;
    long long visited = 0;
    hooks.keep_growing = [](const std::vector<const Purchase*>& order) {
        return order.back()->describe() == "Cheeseburger";
    };
    hooks.visit = [&](const Candidate& c) {
        biggest = std::max(biggest, c.things.size());
        ++visited;
        return true;
    };
    find_candidates(sample, budget_only, with_calories, hooks);
    check(visited == 10 && biggest == 10, "orders stop at 10 things");
}

void test_needed_only(const Chain& sample) {
    // Classic Burger, medium fries, medium drink.
    const Target target = make_target(target_json(
        "sample_burger", {wanted("classic_burger"), wanted("fries", 1, "medium"), wanted("soft_drink", 1, "medium")}));
    const PurchaseList all = list_purchases(sample, false);
    SearchStats stats;
    const std::vector<json> found = all_candidates(sample, target, all, &stats);

    // Things that help: 3 singles, 3 Classic Burger Combos and 3 large ones
    // (all have the burger), 4 Chicken Combos as they come (medium drink),
    // and the 3 pairs in the 2 for $6 with a Classic Burger = 16.
    check(stats.things_to_choose_from == 16, "16 things help with burger, fries and drink");
    // Orders where every thing is needed: 42, counted separately with a
    // short Python script that tries every mix of up to 3 of the 16. For
    // example: the combo alone; the three singles; a Classic Burger Combo
    // with apple slices + medium fries; a Classic Burger Combo with apple
    // slices + a Chicken Combo (for its fries); a 2 for $6 + fries + drink.
    check(stats.candidates == 42, "42 candidate orders (found " + std::to_string(stats.candidates) + ")");

    const json combo_alone = canonical(json::parse(R"([{"combo":"classic_burger_combo","upsize":null,"quantity":1,
        "contents":[{"role":"main","item":"classic_burger","size":"regular"},
                    {"role":"side","item":"fries","size":"medium"},
                    {"role":"drink","item":"soft_drink","size":"medium"}]}])"));
    check(std::find(found.begin(), found.end(), combo_alone) != found.end(), "the combo alone is a candidate");
    for (const auto& order : found)
        for (const auto& line : order)
            check(line.value("item", "") != "cookie" && line.value("bundle", "") != "five_dollar_box",
                  "nothing unrelated is added: " + order.dump());

    // Stopping early.
    long long seen = 0;
    SearchHooks stop_after_3;
    stop_after_3.visit = [&](const Candidate&) { return ++seen < 3; };
    check(find_candidates(sample, target, all, stop_after_3).status == SearchStatus::Stopped && seen == 3,
          "the visitor can stop the search");
}

void test_left_out_items(const Chain& sample) {
    json t = target_json("sample_burger", {wanted("cookie")});
    t["max_calories"] = 500;
    const Target target = make_target(t);
    const PurchaseList list = list_purchases(sample, uses_calories(target));
    SearchStats stats;
    check(all_candidates(sample, target, list, &stats).empty(), "a cookie with a calorie limit: no candidates");
    check(stats.left_out == std::vector<std::string>{"Chocolate Chip Cookie"}, "and the cookie is reported as left out");
}

// Every hand-calculated case (task 8.1): the expected order is a candidate.
void test_optimizer_cases(const fs::path& project) {
    std::ifstream in(project / "tests" / "optimizer_cases.json");
    const json doc = json::parse(in);
    std::map<std::string, Chain> chains;
    for (const auto& folder : doc.at("data_folders"))
        for (const auto& entry : fs::directory_iterator(project / folder.get<std::string>()))
            if (entry.path().extension() == ".json") {
                Chain c = load(entry.path());
                chains[c.id] = c;
            }

    int searched = 0, checked = 0;
    for (const auto& c : doc.at("cases")) {
        const std::string id = c.at("id");
        const Target target = make_target(c.at("target"));
        const Chain& chain = chains.at(*target.chain);
        const json& expected = c.at("expected");
        const std::string result = expected.at("result");
        const PurchaseList list = list_purchases(chain, uses_calories(target));

        if (result == "too_many_items") {
            SearchStats stats = find_candidates(chain, target, list, {});
            check(stats.status == SearchStatus::TooManyItems, id + ": refused for too many items");
            continue;
        }

        std::vector<json> wanted_orders;
        if (result == "order") {
            wanted_orders.push_back(canonical(expected.at("order")));
            for (const auto& alt : expected.value("equal_price_alternatives", json::array()))
                wanted_orders.push_back(canonical(alt));
        }

        if (target.goal == Goal::Cheapest && !target.min_calories) {
            // Small enough to list every candidate.
            const std::vector<json> found = all_candidates(chain, target, list);
            for (const auto& order : wanted_orders)
                check(std::find(found.begin(), found.end(), order) != found.end(),
                      id + ": expected order not among the " + std::to_string(found.size()) + " candidates");
            if (result == "no_valid_order" && !target.max_price_cents)
                check(found.empty(), id + ": no candidates when nothing on the menu fits");
            ++searched;
        } else {
            // Extra things allowed: too many orders to list without the
            // budget limits of tasks 3.3 and 3.4. Instead, check each
            // expected order is made of listed things and covers the target,
            // which is exactly what makes it a candidate.
            std::map<std::string, const Purchase*> by_line;
            for (const auto& p : list.purchases) {
                Candidate one{{&p}, {}};
                by_line[canonical(candidate_to_json(one))[0].dump()] = &p;
            }
            const Coverage coverage(chain, target);
            for (const auto& order : wanted_orders) {
                std::vector<const Purchase*> things;
                bool all_listed = true;
                for (json line : order) {
                    const int quantity = line.at("quantity");
                    line["quantity"] = 1;
                    line.erase("add_ons");
                    if (line.contains("contents"))
                        for (auto& part : line["contents"]) part.erase("add_ons");
                    auto it = by_line.find(canonical(json::array({line}))[0].dump());
                    if (it == by_line.end()) {
                        all_listed = false;
                        check(false, id + ": not a listed thing to order: " + line.dump());
                        continue;
                    }
                    for (int q = 0; q < quantity; ++q) things.push_back(it->second);
                }
                if (all_listed)
                    check(static_cast<int>(things.size()) <= kItemCap && coverage.covers(things),
                          id + ": expected order should be a candidate");
            }
            ++checked;
        }
    }
    check(searched + checked > 30, "optimizer cases were read");
    std::cout << "optimizer cases: " << searched << " searched in full, " << checked << " checked directly\n";
}

}  // namespace

int main(int argc, char** argv) {
    if (argc != 2) {
        std::cerr << "usage: candidates_tests <project folder>\n";
        return 2;
    }
    const fs::path project = argv[1];
    try {
        const Chain sample = load(project / "data" / "sample" / "sample_chain.json");
        test_sample_things_to_order(sample);
        test_upsizes(sample);
        test_pick_two(sample);
        test_coverage(sample);
        test_item_cap(sample);
        test_needed_only(sample);
        test_left_out_items(sample);
        test_optimizer_cases(project);
    } catch (const std::exception& e) {
        std::cerr << "FAIL: " << e.what() << '\n';
        return 1;
    }
    if (failures) {
        std::cerr << failures << " check(s) failed\n";
        return 1;
    }
    std::cout << "all candidate checks passed\n";
    return 0;
}
