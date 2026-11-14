#include "combobite/checks.hpp"

#include <algorithm>
#include <cctype>
#include <map>
#include <set>

namespace combobite {

namespace {

class ChainChecker {
public:
    ChainChecker(const Chain& chain, const Date& today) : chain_(chain), today_(today) {}

    std::vector<Issue> run() {
        check_prices_date();
        check_ids();
        check_items();
        check_add_ons();
        for (const auto& combo : chain_.combos) check_combo(combo);
        for (const auto& bundle : chain_.bundles) check_bundle(bundle);
        return std::move(issues_);
    }

private:
    const Chain& chain_;
    const Date today_;
    std::vector<Issue> issues_;

    void error(const std::string& entry, const std::string& message) {
        issues_.push_back({Severity::Error, entry, message});
    }
    void warning(const std::string& entry, const std::string& message) {
        issues_.push_back({Severity::Warning, entry, message});
    }

    bool has_category(const std::string& category) const {
        return std::find(chain_.categories.begin(), chain_.categories.end(), category) != chain_.categories.end();
    }

    void check_prices_date() {
        if (chain_.format_version != kFormatVersion)
            error("file", "uses format_version " + std::to_string(chain_.format_version) +
                              ", but this version of ComboBite reads format " + std::to_string(kFormatVersion));
        const auto date = parse_date(chain_.prices_checked.date);
        if (!date) {
            error("prices_checked", "date '" + chain_.prices_checked.date +
                                        "' isn't a real date written as YYYY-MM-DD, e.g. 2026-10-30");
            return;
        }
        const long long age = days_between(*date, today_);
        if (age > kStalePriceDays)
            warning("prices_checked", "prices were last checked " + std::to_string(age) + " days ago (" +
                                          chain_.prices_checked.date + "); they may be out of date");
        else if (age < 0)
            warning("prices_checked", "date " + chain_.prices_checked.date + " is in the future");
    }

    void check_ids() {
        std::map<std::string, std::string> owner;  // id -> what uses it
        auto claim = [&](const std::string& id, const std::string& what) {
            auto [it, inserted] = owner.emplace(id, what);
            if (!inserted) error(what, "id '" + id + "' is already used by " + it->second);
        };
        for (const auto& item : chain_.items) claim(item.id, "item '" + item.id + "'");
        for (const auto& add_on : chain_.add_ons) claim(add_on.id, "add-on '" + add_on.id + "'");
        for (const auto& combo : chain_.combos) claim(combo.id, "combo '" + combo.id + "'");
        for (const auto& bundle : chain_.bundles) claim(bundle.id, "bundle '" + bundle.id + "'");

        std::map<std::string, std::string> names;  // lowercase name -> item id
        for (const auto& item : chain_.items) {
            std::string key = item.name;
            std::transform(key.begin(), key.end(), key.begin(),
                           [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            auto [it, inserted] = names.emplace(key, item.id);
            if (!inserted)
                error("item '" + item.id + "'", "has the same name, \"" + item.name + "\", as item '" + it->second + "'");
        }
    }

    void check_items() {
        for (const auto& category : chain_.categories)
            if (std::count(chain_.categories.begin(), chain_.categories.end(), category) > 1) {
                error("categories", "lists '" + category + "' more than once");
                break;
            }

        for (const auto& item : chain_.items) {
            const std::string entry = "item '" + item.id + "'";
            if (!has_category(item.category))
                error(entry, "has category '" + item.category + "', which isn't in \"categories\"");
            if (item.sizes.empty()) error(entry, "has no sizes; single-size items use one size named \"regular\"");

            std::set<std::string> seen;
            std::vector<std::string> no_calories;
            for (const auto& size : item.sizes) {
                const std::string where = entry + ", size '" + size.size + "'";
                if (!seen.insert(size.size).second) error(entry, "lists size '" + size.size + "' more than once");
                if (size.price_cents == 0) error(where, "has a price of $0.00");
                if (size.price_cents < 0) error(where, "has a negative price, " + format_money(size.price_cents));
                if (size.calories && *size.calories < 0) error(where, "has negative calories");
                if (!size.calories) no_calories.push_back(size.size);
            }
            if (!no_calories.empty()) {
                std::string which;
                if (no_calories.size() < item.sizes.size()) {
                    which = " for ";
                    for (std::size_t i = 0; i < no_calories.size(); ++i)
                        which += (i ? ", " : "") + no_calories[i];
                }
                warning(entry, "has no calorie data" + which +
                                   "; it's left out of calorie searches but still works for price-only requests");
            }
        }
    }

    void check_add_ons() {
        for (const auto& add_on : chain_.add_ons) {
            const std::string entry = "add-on '" + add_on.id + "'";
            if (add_on.price_cents < 0) error(entry, "has a negative price, " + format_money(add_on.price_cents));
            if (add_on.calories && *add_on.calories < 0) error(entry, "has negative calories");
            if (add_on.max_per_item < 1) error(entry, "\"max_per_item\" must be 1 or more");
            for (const auto& id : add_on.applies_to_items)
                if (!find_item(chain_, id))
                    error(entry, "applies to item '" + id + "', which this file doesn't define");
            for (const auto& category : add_on.applies_to_categories)
                if (!has_category(category))
                    error(entry, "applies to category '" + category + "', which isn't in \"categories\"");
            if (add_on.applies_to_items.empty() && add_on.applies_to_categories.empty())
                warning(entry, "applies to nothing; add \"applies_to_items\" or \"applies_to_categories\"");
        }
    }

    // Checks every slot; returns false when a choice can't be resolved.
    bool check_slots(const std::vector<Slot>& slots, const std::string& entry) {
        bool resolved = true;
        if (slots.empty()) error(entry, "has no slots");
        std::set<std::string> roles;
        for (const auto& slot : slots) {
            const std::string where = entry + ", slot '" + slot.role + "'";
            if (!roles.insert(slot.role).second) error(entry, "has two slots named '" + slot.role + "'");
            if (slot.count < 1) error(where, "\"count\" must be 1 or more");
            if (slot.choices.empty()) {
                error(where, "has no choices");
                resolved = false;
            }
            for (const auto& choice : slot.choices) {
                const Item* item = find_item(chain_, choice.item);
                if (!item) {
                    error(where, "uses item '" + choice.item +
                                     "', which this file doesn't define (was it removed from the menu?)");
                    resolved = false;
                    continue;
                }
                if (find_size(*item, choice.size)) continue;
                resolved = false;
                if (choice.size.empty())
                    error(where, "must say which size of '" + choice.item + "' it uses; it comes in " +
                                     size_names(*item));
                else
                    error(where, "uses size '" + choice.size + "' of '" + choice.item + "', which only comes in " +
                                     size_names(*item));
            }
        }
        return resolved;
    }

    // The most a customer could pay for a deal's items bought one at a time,
    // after the deal's own extra charges. If the deal costs at least that
    // much, it can never save money.
    Cents best_separate_price(const std::vector<Slot>& slots) const {
        Cents total = 0;
        for (const auto& slot : slots) {
            Cents best = 0;
            bool first = true;
            for (const auto& choice : slot.choices) {
                const Item* item = find_item(chain_, choice.item);
                const SizeOption* size = item ? find_size(*item, choice.size) : nullptr;
                if (!size) continue;
                const Cents value = size->price_cents - choice.extra_cents;
                if (first || value > best) best = value;
                first = false;
            }
            total += best * slot.count;
        }
        return total;
    }

    void check_deal_saves(const std::string& entry, Cents price, const std::vector<Slot>& slots, const char* kind) {
        const Cents separate = best_separate_price(slots);
        if (price > separate)
            warning(entry, "costs " + format_money(price) + ", more than its items bought separately (" +
                               format_money(separate) + "), so it never saves money; check for a typo");
        else if (price == separate)
            warning(entry, std::string("costs the same as its items bought separately (") + format_money(price) +
                               "), so this " + kind + " never saves money");
    }

    void check_combo(const Combo& combo) {
        const std::string entry = "combo '" + combo.id + "'";
        if (combo.price_cents == 0) error(entry, "has a price of $0.00");
        if (combo.price_cents < 0) error(entry, "has a negative price, " + format_money(combo.price_cents));
        const bool resolved = check_slots(combo.slots, entry);

        for (const auto& upsize : combo.upsizes) {
            const std::string where = entry + ", upsize '" + upsize.name + "'";
            if (upsize.extra_cents < 0) error(where, "has a negative charge, " + format_money(upsize.extra_cents));
            for (const auto& resize : upsize.resize) {
                // Plain references, since C++17 lambdas can't capture structured bindings.
                const std::string& role = resize.first;
                const std::string& size = resize.second;
                auto slot = std::find_if(combo.slots.begin(), combo.slots.end(),
                                         [&role](const Slot& s) { return s.role == role; });
                if (slot == combo.slots.end()) {
                    error(where, "resizes slot '" + role + "', but the combo has no slot with that role");
                    continue;
                }
                const bool any = std::any_of(slot->choices.begin(), slot->choices.end(), [&](const SlotChoice& c) {
                    const Item* item = find_item(chain_, c.item);
                    return item && find_size(*item, size);
                });
                if (!any)
                    error(where, "resizes '" + role + "' to '" + size + "', but nothing in that slot comes in '" +
                                     size + "'");
            }
        }
        if (resolved && combo.price_cents > 0) check_deal_saves(entry, combo.price_cents, combo.slots, "combo");
    }

    void check_bundle(const Bundle& bundle) {
        const std::string entry = "bundle '" + bundle.id + "'";
        if (bundle.price_cents == 0) error(entry, "has a price of $0.00");
        if (bundle.price_cents < 0) error(entry, "has a negative price, " + format_money(bundle.price_cents));
        const bool resolved = check_slots(bundle.slots, entry);
        if (resolved && bundle.price_cents > 0) check_deal_saves(entry, bundle.price_cents, bundle.slots, "bundle");
    }

    static std::string size_names(const Item& item) {
        std::string names;
        for (std::size_t i = 0; i < item.sizes.size(); ++i) names += (i ? ", " : "") + item.sizes[i].size;
        return names;
    }
};

}  // namespace

std::vector<Issue> check_chain(const Chain& chain, const Date& today) {
    return ChainChecker(chain, today).run();
}

std::string format_money(Cents cents) {
    const bool negative = cents < 0;
    const Cents abs = negative ? -cents : cents;
    std::string fraction = std::to_string(abs % 100);
    if (fraction.size() < 2) fraction = "0" + fraction;
    return std::string(negative ? "-" : "") + "$" + std::to_string(abs / 100) + "." + fraction;
}

}  // namespace combobite
