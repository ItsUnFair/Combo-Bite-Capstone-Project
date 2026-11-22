// Generating candidate orders (task 3.1).
//
// The optimizer works in three steps:
//   1. Candidates (this file): list every thing that can be ordered from a
//      chain, then build every order of up to 10 things that covers the
//      target's items.
//   2. Pricing (task 3.2): add up each candidate's cost and calories.
//   3. Choosing (task 3.3): drop candidates outside the budget or calorie
//      range and pick the best one.
//
// The rules are in docs/optimizer-rules.md.
#pragma once

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "combobite/menu.hpp"
#include "combobite/target.hpp"

namespace combobite {

// An order can have at most this many things to order (a combo counts as one).
inline constexpr int kItemCap = 10;

// ------------------------------------------------------- things to order ---

enum class PurchaseKind { Single, Combo, Bundle };

// One item someone gets: a single item, or one part of a combo or bundle.
struct Part {
    std::string role;             // slot role, e.g. "side"; empty for a single item
    const Item* item = nullptr;
    const SizeOption* size = nullptr;  // the size they get, after any upsize
    Cents extra_cents = 0;        // charge for this slot choice (combos and bundles)
};

// One thing to order: one size of an item, or a combo or bundle with every
// slot filled (and a combo made large or not).
struct Purchase {
    PurchaseKind kind = PurchaseKind::Single;
    const Item* item = nullptr;            // Single
    const Combo* combo = nullptr;          // Combo
    const ComboUpsize* upsize = nullptr;   // Combo made large, or nullptr
    const Bundle* bundle = nullptr;        // Bundle
    std::vector<Part> parts;               // a single item has one part

    const std::string& id() const;
    std::string describe() const;  // e.g. "Big Mac Meal, large (Big Mac, large Fries, large Coca-Cola)"
};

// Everything that can be ordered from a chain.
struct PurchaseList {
    std::vector<Purchase> purchases;
    // Item sizes left out because they have no calorie data (only when the
    // target uses calories). Each one is counted once, even if it's a choice
    // in several combos.
    std::vector<std::string> left_out;
};

// Lists every thing to order: each size of each item, each combo with every
// mix of slot choices (as it comes and made large), and each bundle with
// every mix of choices. A slot with count 2 gets every pair of choices, in
// either order counted once. When leave_out_no_calories is true, any item
// size with no calorie data is skipped, as a single item and as a choice.
PurchaseList list_purchases(const Chain& chain, bool leave_out_no_calories);

// ----------------------------------------------------------- candidates ----

// Where a requested add-on goes: on part `part` of thing `thing`.
struct AddOnPlacement {
    int thing = 0;
    int part = 0;
    const AddOn* add_on = nullptr;
};

// One candidate order. `things` point into the PurchaseList used to build it.
struct Candidate {
    std::vector<const Purchase*> things;
    std::vector<AddOnPlacement> add_ons;
};

// True when the target has a calorie limit or the goal is most calories.
bool uses_calories(const Target& target);

// How many units the target asks for in total (quantities added up).
int wanted_units(const Target& target);

// Matches a target's wanted items to the parts of an order.
class Coverage {
public:
    // Throws std::invalid_argument when the target names an add-on the
    // chain doesn't have.
    Coverage(const Chain& chain, const Target& target);

    // Can every wanted unit be matched to a different part of the order?
    // On success, fills `add_ons` (when given) with where each requested
    // add-on goes.
    bool covers(const std::vector<const Purchase*>& things,
                std::vector<AddOnPlacement>* add_ons = nullptr) const;

    // Does at least one part of this thing fit at least one wanted unit?
    bool helps(const Purchase& purchase) const;

    int units() const { return static_cast<int>(wanted_.size()); }

private:
    bool fits(int wanted, const Part& part) const;

    struct Wanted {
        WantedItem item;  // a copy, so the target can be a temporary
        std::vector<const AddOn*> add_ons;
    };
    std::vector<Wanted> wanted_;
};

enum class SearchStatus {
    Done,          // every candidate was visited
    Stopped,       // the visitor asked to stop
    TooManyItems,  // the target wants more than kItemCap units; nothing visited
};

struct SearchStats {
    SearchStatus status = SearchStatus::Done;
    long long candidates = 0;  // candidate orders visited
    long long partial_orders = 0;  // orders looked at along the way
    int things_to_choose_from = 0;
    std::vector<std::string> left_out;  // see PurchaseList::left_out
};

struct SearchHooks {
    // Called for every candidate. Return false to stop the search.
    std::function<bool(const Candidate&)> visit;
    // Called before an order is grown by one more thing. Return false to
    // skip everything that starts with it. Tasks 3.3 and 3.4 use this to cut
    // the search short (over budget, can't beat the best so far). Optional.
    std::function<bool(const std::vector<const Purchase*>&)> keep_growing;
};

// Builds every candidate order for the target and passes each one to
// hooks.visit. `purchases` must come from list_purchases for the same chain,
// with leave_out_no_calories = uses_calories(target).
//
// Which orders count as candidates:
//   - at most kItemCap things, at least one;
//   - every wanted unit matched to a different part (Coverage::covers);
//   - for the goal "cheapest" with no calorie minimum, every thing in the
//     order is needed: taking any one away breaks the coverage. An extra
//     thing could only add cost, so those orders are never the answer.
//   - otherwise (a calorie minimum or the goal "most calories"), extra
//     things are allowed, since they add calories.
// Each mix of things is visited once, whatever order they're listed in.
SearchStats find_candidates(const Chain& chain, const Target& target, const PurchaseList& purchases,
                            const SearchHooks& hooks);

// The order in words, e.g. "Big Mac Meal (Big Mac, medium Fries, medium
// Coca-Cola) + 2 x Cheeseburger with Add Cheese".
std::string describe_candidate(const Candidate& candidate);

// The order as JSON lines, in the format used by tests/optimizer_cases.json:
// identical things are grouped with a quantity.
nlohmann::json candidate_to_json(const Candidate& candidate);

}  // namespace combobite
