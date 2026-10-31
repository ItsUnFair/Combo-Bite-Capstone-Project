// Task 6.1 test: the sample chain file can be written in the format,
// parses into the C++ structs, survives a round trip back to JSON,
// and every combo and bundle choice points at a real item and size.
//
// Usage: engine_tests <path to chain file>
#include <fstream>
#include <iostream>
#include <string>

#include "combobite/menu.hpp"

using namespace combobite;

namespace {

int failures = 0;

void check(bool ok, const std::string& what) {
    if (!ok) {
        ++failures;
        std::cerr << "FAIL: " << what << '\n';
    }
}

void check_choices(const Chain& chain, const std::string& deal, const std::vector<Slot>& slots) {
    for (const auto& slot : slots) {
        check(!slot.choices.empty(), deal + " slot '" + slot.role + "' has choices");
        check(slot.count >= 1, deal + " slot '" + slot.role + "' count is at least 1");
        for (const auto& choice : slot.choices) {
            const Item* item = find_item(chain, choice.item);
            check(item != nullptr, deal + " uses defined item '" + choice.item + "'");
            if (item)
                check(find_size(*item, choice.size) != nullptr,
                      deal + " uses a real size of '" + choice.item + "'");
        }
    }
}

}  // namespace

int main(int argc, char** argv) {
    if (argc != 2) {
        std::cerr << "usage: engine_tests <chain file>\n";
        return 2;
    }
    std::ifstream in(argv[1]);
    if (!in) {
        std::cerr << "cannot open " << argv[1] << '\n';
        return 2;
    }

    const nlohmann::json original = nlohmann::json::parse(in);
    const Chain chain = original.get<Chain>();

    check(chain.format_version == kFormatVersion, "format version matches");
    check(!chain.id.empty() && !chain.name.empty(), "chain id and name are read");
    check(!chain.items.empty(), "the chain has items");
    for (const auto& item : chain.items) {
        check(!item.sizes.empty(), "item '" + item.id + "' has at least one size");
        for (const auto& s : item.sizes)
            check(s.price_cents > 0, "item '" + item.id + "' size '" + s.size + "' has a price");
    }
    for (const auto& c : chain.combos) check_choices(chain, "combo " + c.id, c.slots);
    for (const auto& b : chain.bundles) check_choices(chain, "bundle " + b.id, b.slots);

    // Round trip: structs -> JSON -> structs gives the same JSON again.
    const nlohmann::json once = chain;
    const nlohmann::json twice = once.get<Chain>();
    check(once == twice, "round trip through JSON is stable");

    if (chain.id != "sample_burger") {
        if (failures == 0) std::cout << chain.id << ": all checks passed\n";
        return failures == 0 ? 0 : 1;
    }

    // Extra checks that only make sense for the sample file.
    check(chain.items.size() == 10, "all 10 items are read");
    check(chain.combos.size() == 2, "both combos are read");
    check(chain.bundles.size() == 2, "both bundles are read");
    check(chain.add_ons.size() == 2, "both add-ons are read");

    const Item* cookie = find_item(chain, "cookie");
    check(cookie && !cookie->sizes.front().calories, "null calories become 'no data'");

    const Item* fries = find_item(chain, "fries");
    check(fries && fries->sizes.size() == 3, "multi-size item keeps every size");

    const Combo& combo = chain.combos.front();
    check(combo.upsizes.size() == 1 && combo.upsizes.front().resize.size() == 2,
          "combo upsize keeps its resize rules");
    check(combo.slots[1].choices[2].extra_cents == 100, "substitution charge is read");
    check(chain.bundles.front().slots.front().count == 2, "bundle pick count is read");

    if (failures == 0) std::cout << "menu format: all checks passed\n";
    return failures == 0 ? 0 : 1;
}
