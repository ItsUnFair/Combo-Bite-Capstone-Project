// Task 3.2 tests: pricing things to order and whole orders.
//
// The sample chain's prices below were added up by hand from
// data/sample/sample_chain.json. Then every hand-calculated case in
// tests/optimizer_cases.json (task 8.1) is priced, and its total, calories
// and one-at-a-time cost must match the hand calculation to the cent.
//
// Usage: pricing_tests <project folder>
#include <algorithm>
#include <climits>
#include <string>
#include <vector>

#include "combobite/pricing.hpp"
#include "test_helpers.hpp"

using namespace combobite;
using namespace test;

namespace {

// Checks one thing to order by its description.
void expect_price(const PurchaseList& list, const std::string& text, Cents price, Cents separately, int calories,
                  bool complete = true) {
    const Purchase* p = find_by_text(list, text);
    if (!p) {
        check(false, "not on the menu: " + text);
        return;
    }
    const PurchasePrice got = price_purchase(*p);
    check(got.price_cents == price, text + ": price " + format_money(got.price_cents));
    check(got.separately_cents == separately, text + ": separately " + format_money(got.separately_cents));
    check(got.calories == calories, text + ": calories " + std::to_string(got.calories));
    check(got.calories_complete == complete, text + ": calories complete?");
}

void test_things_to_order(const Chain& sample) {
    const PurchaseList list = list_purchases(sample, false);
    // Single item: the size's price.
    expect_price(list, "large Fries", 379, 379, 480);
    // Large combo with the salad swap: $8.49 + $1.20 upsize + $1.00 swap.
    // Separately: $4.49 + $2.49 + large drink $2.19. The salad stays regular.
    expect_price(list, "Classic Burger Combo, large (Classic Burger, Side Salad, large Soft Drink)", 1069, 917,
                 540 + 60 + 290);
    // Combo as it comes: $8.49. Separately $4.49 + $3.09 + $1.79.
    expect_price(list, "Classic Burger Combo (Classic Burger, medium Fries, medium Soft Drink)", 849, 937,
                 540 + 320 + 210);
    // Box with the double cheeseburger: $5.00 + $1.00 upcharge.
    expect_price(list, "$5 Snack Box (Double Cheeseburger, 4 pc Chicken Nuggets, small Fries, small Soft Drink)",
                 600, 349 + 229 + 199 + 129, 450 + 170 + 230 + 150);
    // 2 for $6 with two different picks.
    expect_price(list, "2 for $6 Mix & Match (Classic Burger, 6 pc Chicken Nuggets)", 600, 449 + 329, 540 + 250);
    // The cookie has no calorie data.
    expect_price(list, "Chocolate Chip Cookie", 119, 119, 0, false);
}

void test_add_ons(const Chain& sample) {
    const PurchaseList list = list_purchases(sample, false);

    // Bacon on the combo's burger: $8.49 + $1.29.
    const Target bacon = make_target(target_json("sample_burger", {wanted("classic_burger", 1, nullptr, {"add_bacon"}),
                                                                   wanted("fries", 1, "medium"),
                                                                   wanted("soft_drink", 1, "medium")}));
    Candidate c;
    c.things = {find_by_text(list, "Classic Burger Combo (Classic Burger, medium Fries, medium Soft Drink)")};
    check(Coverage(sample, bacon).covers(c.things, &c.add_ons), "bacon combo covers its target");
    const PricedOrder o = price_order(c);
    check(o.total_cents == 978 && o.add_ons_cents == 129, "bacon combo: $9.78 with $1.29 of add-ons");
    check(o.separately_cents == 1066 && o.savings_cents() == 88, "bacon combo: $10.66 separately, saves $0.88");
    check(o.calories == 1160 && o.calories_complete, "bacon combo: 1,070 + 90 = 1,160 calories");
    check(o.things == 1, "bacon combo: one thing to order");

    // Cheese twice: $2.49 + 2 x $0.60.
    const Target cheese = make_target(
        target_json("sample_burger", {wanted("cheeseburger", 1, nullptr, {"add_cheese", "add_cheese"})}));
    Candidate d;
    d.things = {find_by_text(list, "Cheeseburger")};
    check(Coverage(sample, cheese).covers(d.things, &d.add_ons), "double cheese covers its target");
    const PricedOrder e = price_order(d);
    check(e.total_cents == 369 && e.calories == 400, "double cheese: $3.69, 400 calories");

    check(requested_add_ons_cents(sample, bacon) == 129, "requested add-ons: one bacon");
    check(requested_add_ons_cents(sample, make_target(target_json(
              "sample_burger", {wanted("classic_burger", 2, nullptr, {"add_bacon", "add_cheese"})}))) ==
              2 * (129 + 60),
          "requested add-ons: counted once per unit");
    bool threw = false;
    try {
        requested_add_ons_cents(sample, make_target(target_json(
                                            "sample_burger", {wanted("classic_burger", 1, nullptr, {"add_gold"})})));
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    check(threw, "an add-on the chain doesn't have is an error");
}

// Every hand-calculated case: price the expected order and compare. For
// cases with no budget or calorie limit, also price every candidate and
// check the cheapest one costs what the hand calculation says.
void test_optimizer_cases(const fs::path& project) {
    const OptimizerCases cases = load_optimizer_cases(project);
    int priced = 0, cheapest_found = 0;
    for (const auto& c : cases.doc.at("cases")) {
        const json& expected = c.at("expected");
        if (expected.at("result") != "order") continue;
        const std::string id = c.at("id");
        const Target target = make_target(c.at("target"));
        const Chain& chain = cases.chains.at(*target.chain);
        const PurchaseList list = list_purchases(chain, uses_calories(target));
        const Coverage coverage(chain, target);

        std::vector<json> orders{expected.at("order")};
        for (const auto& alt : expected.value("equal_price_alternatives", json::array())) orders.push_back(alt);
        for (std::size_t i = 0; i < orders.size(); ++i) {
            Candidate candidate;
            std::string missing;
            if (!order_things(list, orders[i], candidate.things, missing) ||
                !coverage.covers(candidate.things, &candidate.add_ons)) {
                check(false, id + ": expected order isn't a valid candidate " + missing);
                continue;
            }
            const PricedOrder o = price_order(candidate);
            const Cents want = expected.at("total_cents");
            check(o.total_cents == want, id + ": priced at " + format_money(o.total_cents) + ", hand total " +
                                             format_money(want));
            if (i == 0) {  // calories and the one-at-a-time cost are for the main expected order
                check(o.calories == expected.at("calories").get<int>(),
                      id + ": calories " + std::to_string(o.calories));
                check(o.calories_complete == expected.at("calories_complete").get<bool>(), id + ": calories complete?");
                check(o.separately_cents == expected.at("separately_cents").get<Cents>(),
                      id + ": separately " + format_money(o.separately_cents));
                check(o.savings_cents() >= 0, id + ": a deal that costs more than its parts never wins");
            }
        }
        ++priced;

        const bool no_limits = target.goal == Goal::Cheapest && !target.min_calories && !target.max_calories &&
                               !target.max_price_cents;
        if (no_limits) {
            Cents cheapest = LLONG_MAX;
            SearchHooks hooks;
            hooks.visit = [&](const Candidate& cand) {
                cheapest = std::min(cheapest, price_order(cand).total_cents);
                return true;
            };
            find_candidates(chain, target, list, hooks);
            check(cheapest == expected.at("total_cents").get<Cents>(),
                  id + ": cheapest candidate is " + format_money(cheapest));
            ++cheapest_found;
        }
    }
    check(priced >= 30, "optimizer cases were priced");
    std::cout << "optimizer cases: " << priced << " expected orders priced, cheapest candidate matched in "
              << cheapest_found << '\n';
}

}  // namespace

int main(int argc, char** argv) {
    if (argc != 2) {
        std::cerr << "usage: pricing_tests <project folder>\n";
        return 2;
    }
    const fs::path project = argv[1];
    try {
        const Chain sample = load(project / "data" / "sample" / "sample_chain.json");
        test_things_to_order(sample);
        test_add_ons(sample);
        test_optimizer_cases(project);
    } catch (const std::exception& e) {
        std::cerr << "FAIL: " << e.what() << '\n';
        return 1;
    }
    if (failures) {
        std::cerr << failures << " check(s) failed\n";
        return 1;
    }
    std::cout << "all pricing checks passed\n";
    return 0;
}
