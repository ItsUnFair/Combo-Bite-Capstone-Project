// Generating candidate orders (task 3.1). See candidates.hpp and
// docs/optimizer-rules.md.
#include "combobite/candidates.hpp"

#include <algorithm>
#include <stdexcept>

namespace combobite {

namespace {

// "medium Fries", or just "Big Mac" for an item's only size "regular".
std::string part_name(const Part& part) {
    if (part.size->size == "regular") return part.item->name;
    return part.size->size + " " + part.item->name;
}

// Every way to fill `count` copies of a slot from `choices`, with repeats,
// each mix once: for choices {A, B} and count 2 that's AA, AB, BB.
void choose_with_repeats(const std::vector<Part>& choices, int count, std::size_t start, std::vector<Part>& picked,
                         std::vector<std::vector<Part>>& out) {
    if (static_cast<int>(picked.size()) == count) {
        out.push_back(picked);
        return;
    }
    for (std::size_t i = start; i < choices.size(); ++i) {
        picked.push_back(choices[i]);
        choose_with_repeats(choices, count, i, picked, out);
        picked.pop_back();
    }
}

class PurchaseBuilder {
public:
    PurchaseBuilder(const Chain& chain, bool leave_out) : chain_(chain), leave_out_(leave_out) {}

    PurchaseList build() {
        for (const auto& item : chain_.items)
            for (const auto& size : item.sizes)
                if (usable(item, size)) {
                    Purchase p;
                    p.kind = PurchaseKind::Single;
                    p.item = &item;
                    p.parts.push_back(Part{"", &item, &size, 0});
                    list_.purchases.push_back(std::move(p));
                }
        for (const auto& combo : chain_.combos) {
            add_deal(combo.slots, nullptr, [&](Purchase& p) { p.kind = PurchaseKind::Combo; p.combo = &combo; });
            for (const auto& upsize : combo.upsizes)
                add_deal(combo.slots, &upsize, [&](Purchase& p) {
                    p.kind = PurchaseKind::Combo;
                    p.combo = &combo;
                    p.upsize = &upsize;
                });
        }
        for (const auto& bundle : chain_.bundles)
            add_deal(bundle.slots, nullptr, [&](Purchase& p) { p.kind = PurchaseKind::Bundle; p.bundle = &bundle; });
        return std::move(list_);
    }

private:
    // False (and noted once) when the size has no calories and those are left out.
    bool usable(const Item& item, const SizeOption& size) {
        if (!leave_out_ || size.calories) return true;
        std::string name = size.size == "regular" ? item.name : item.name + " (" + size.size + ")";
        if (std::find(list_.left_out.begin(), list_.left_out.end(), name) == list_.left_out.end())
            list_.left_out.push_back(name);
        return false;
    }

    // The part a slot choice gives, after any upsize; nullopt if unusable.
    std::optional<Part> part_for(const Slot& slot, const SlotChoice& choice, const ComboUpsize* upsize) {
        const Item* item = find_item(chain_, choice.item);
        if (!item) return std::nullopt;
        const SizeOption* size = find_size(*item, choice.size);
        if (!size) return std::nullopt;
        if (upsize && !choice.size.empty()) {
            for (const auto& [role, new_size] : upsize->resize)
                if (role == slot.role)
                    if (const SizeOption* bigger = find_size(*item, new_size)) size = bigger;
        }
        if (!usable(*item, *size)) return std::nullopt;
        return Part{slot.role, item, size, choice.extra_cents};
    }

    template <typename Fill>
    void add_deal(const std::vector<Slot>& slots, const ComboUpsize* upsize, Fill fill) {
        // Every way to fill each slot on its own...
        std::vector<std::vector<std::vector<Part>>> per_slot;
        for (const auto& slot : slots) {
            std::vector<Part> choices;
            for (const auto& choice : slot.choices)
                if (auto part = part_for(slot, choice, upsize)) choices.push_back(*part);
            std::vector<std::vector<Part>> fills;
            std::vector<Part> picked;
            choose_with_repeats(choices, slot.count, 0, picked, fills);
            if (fills.empty()) return;  // a slot nobody can fill: the deal can't be used
            per_slot.push_back(std::move(fills));
        }
        // ...then every mix of those across the slots.
        std::vector<std::size_t> at(per_slot.size(), 0);
        while (true) {
            Purchase p;
            fill(p);
            for (std::size_t s = 0; s < per_slot.size(); ++s)
                for (const auto& part : per_slot[s][at[s]]) p.parts.push_back(part);
            list_.purchases.push_back(std::move(p));
            std::size_t s = 0;
            while (s < at.size() && ++at[s] == per_slot[s].size()) at[s++] = 0;
            if (s == at.size()) break;
        }
    }

    const Chain& chain_;
    bool leave_out_;
    PurchaseList list_;
};

}  // namespace

// ------------------------------------------------------------- Purchase ----

const std::string& Purchase::id() const {
    switch (kind) {
        case PurchaseKind::Combo: return combo->id;
        case PurchaseKind::Bundle: return bundle->id;
        default: return item->id;
    }
}

std::string Purchase::describe() const {
    if (kind == PurchaseKind::Single) return part_name(parts.front());
    std::string text = kind == PurchaseKind::Combo ? combo->name : bundle->name;
    if (upsize) text += ", " + upsize->name;
    text += " (";
    for (std::size_t i = 0; i < parts.size(); ++i) text += (i ? ", " : "") + part_name(parts[i]);
    return text + ")";
}

PurchaseList list_purchases(const Chain& chain, bool leave_out_no_calories) {
    return PurchaseBuilder(chain, leave_out_no_calories).build();
}

// ------------------------------------------------------------- Coverage ----

bool uses_calories(const Target& target) {
    return target.min_calories || target.max_calories || target.goal == Goal::MostCalories;
}

int wanted_units(const Target& target) {
    long long units = 0;
    for (const auto& w : target.items) units += w.quantity;
    return static_cast<int>(std::min<long long>(units, 1'000'000));
}

Coverage::Coverage(const Chain& chain, const Target& target) {
    for (const auto& w : target.items) {
        Wanted unit{w, {}};
        for (const auto& id : w.add_ons) {
            auto it = std::find_if(chain.add_ons.begin(), chain.add_ons.end(),
                                   [&](const AddOn& a) { return a.id == id; });
            if (it == chain.add_ons.end())
                throw std::invalid_argument("the target asks for add-on '" + id + "', which " + chain.name +
                                            " doesn't have");
            unit.add_ons.push_back(&*it);
        }
        for (int q = 0; q < w.quantity && static_cast<int>(wanted_.size()) <= kItemCap; ++q) wanted_.push_back(unit);
    }
}

bool Coverage::fits(int wanted, const Part& part) const {
    const WantedItem& w = wanted_[static_cast<std::size_t>(wanted)].item;
    if (w.item && part.item->id != *w.item) return false;
    if (w.category && part.item->category != *w.category) return false;
    if (w.size && part.size->size != *w.size) return false;
    const auto& add_ons = wanted_[static_cast<std::size_t>(wanted)].add_ons;
    for (const AddOn* add_on : add_ons) {
        const auto& cats = add_on->applies_to_categories;
        const auto& items = add_on->applies_to_items;
        const bool applies = std::find(cats.begin(), cats.end(), part.item->category) != cats.end() ||
                             std::find(items.begin(), items.end(), part.item->id) != items.end();
        if (!applies) return false;
        if (std::count(add_ons.begin(), add_ons.end(), add_on) > add_on->max_per_item) return false;
    }
    return true;
}

bool Coverage::helps(const Purchase& purchase) const {
    for (const auto& part : purchase.parts)
        for (int w = 0; w < units(); ++w)
            if (fits(w, part)) return true;
    return false;
}

bool Coverage::covers(const std::vector<const Purchase*>& things, std::vector<AddOnPlacement>* add_ons) const {
    struct PartRef { int thing, part; const Part* p; };
    std::vector<PartRef> parts;
    for (std::size_t t = 0; t < things.size(); ++t)
        for (std::size_t p = 0; p < things[t]->parts.size(); ++p)
            parts.push_back({static_cast<int>(t), static_cast<int>(p), &things[t]->parts[p]});
    if (parts.size() < wanted_.size()) return false;

    // Which parts each wanted unit fits, then a standard matching: each unit
    // takes a free part, or moves an earlier unit to another part it fits.
    std::vector<std::vector<int>> options(wanted_.size());
    for (std::size_t w = 0; w < wanted_.size(); ++w) {
        for (std::size_t p = 0; p < parts.size(); ++p)
            if (fits(static_cast<int>(w), *parts[p].p)) options[w].push_back(static_cast<int>(p));
        if (options[w].empty()) return false;
    }
    std::vector<int> taken_by(parts.size(), -1);
    std::function<bool(int, std::vector<char>&)> place = [&](int w, std::vector<char>& seen) {
        for (int p : options[static_cast<std::size_t>(w)]) {
            if (seen[static_cast<std::size_t>(p)]) continue;
            seen[static_cast<std::size_t>(p)] = 1;
            if (taken_by[static_cast<std::size_t>(p)] < 0 || place(taken_by[static_cast<std::size_t>(p)], seen)) {
                taken_by[static_cast<std::size_t>(p)] = w;
                return true;
            }
        }
        return false;
    };
    for (std::size_t w = 0; w < wanted_.size(); ++w) {
        std::vector<char> seen(parts.size(), 0);
        if (!place(static_cast<int>(w), seen)) return false;
    }

    if (add_ons) {
        add_ons->clear();
        for (std::size_t p = 0; p < parts.size(); ++p) {
            if (taken_by[p] < 0) continue;
            for (const AddOn* add_on : wanted_[static_cast<std::size_t>(taken_by[p])].add_ons)
                add_ons->push_back({parts[p].thing, parts[p].part, add_on});
        }
    }
    return true;
}

// ------------------------------------------------------------ the search ---

SearchStats find_candidates(const Chain& chain, const Target& target, const PurchaseList& purchases,
                            const SearchHooks& hooks) {
    SearchStats stats;
    stats.left_out = purchases.left_out;
    if (wanted_units(target) > kItemCap) {
        stats.status = SearchStatus::TooManyItems;
        return stats;
    }

    const Coverage coverage(chain, target);
    // With the goal "cheapest" and no calorie minimum, only orders where
    // every thing is needed count, so only things that help are tried and an
    // order never needs more things than there are wanted units.
    const bool needed_only = target.goal == Goal::Cheapest && !target.min_calories;
    std::vector<const Purchase*> options;
    for (const auto& p : purchases.purchases)
        if (!needed_only || coverage.helps(p)) options.push_back(&p);
    stats.things_to_choose_from = static_cast<int>(options.size());
    const int max_things = needed_only ? std::min(kItemCap, coverage.units()) : kItemCap;

    std::vector<const Purchase*> order;
    Candidate candidate;
    bool stop = false;

    auto every_thing_needed = [&]() {
        std::vector<const Purchase*> without;
        for (std::size_t skip = 0; skip < order.size(); ++skip) {
            without.clear();
            for (std::size_t i = 0; i < order.size(); ++i)
                if (i != skip) without.push_back(order[i]);
            if (coverage.covers(without)) return false;
        }
        return true;
    };

    // Grows the order one thing at a time. Things are only added at or after
    // `start`, so each mix of things is built once.
    std::function<void(std::size_t)> grow = [&](std::size_t start) {
        for (std::size_t k = start; k < options.size() && !stop; ++k) {
            order.push_back(options[k]);
            ++stats.partial_orders;
            if (!hooks.keep_growing || hooks.keep_growing(order)) {
                const bool covered = coverage.covers(order, &candidate.add_ons);
                if (covered && (!needed_only || every_thing_needed())) {
                    candidate.things = order;
                    ++stats.candidates;
                    if (hooks.visit && !hooks.visit(candidate)) stop = true;
                }
                // In "needed only" mode, adding to an order that already
                // covers everything would make that thing unneeded.
                if (!stop && static_cast<int>(order.size()) < max_things && !(needed_only && covered)) grow(k);
            }
            order.pop_back();
        }
    };
    if (max_things > 0) grow(0);
    if (stop) stats.status = SearchStatus::Stopped;
    return stats;
}

// ---------------------------------------------------- showing an order ---

namespace {

// Identical things (same thing, same add-ons in the same places) grouped
// into one line with a quantity, in the order they first appear.
struct Line {
    const Purchase* thing;
    std::vector<std::pair<int, const AddOn*>> add_ons;  // (part, add-on)
    int quantity;
};

std::vector<Line> group_lines(const Candidate& candidate) {
    std::vector<Line> lines;
    for (std::size_t t = 0; t < candidate.things.size(); ++t) {
        std::vector<std::pair<int, const AddOn*>> add_ons;
        for (const auto& a : candidate.add_ons)
            if (a.thing == static_cast<int>(t)) add_ons.emplace_back(a.part, a.add_on);
        std::sort(add_ons.begin(), add_ons.end(), [](const auto& a, const auto& b) {
            return a.first != b.first ? a.first < b.first : a.second->id < b.second->id;
        });
        auto same = std::find_if(lines.begin(), lines.end(), [&](const Line& l) {
            return l.thing == candidate.things[t] && l.add_ons == add_ons;
        });
        if (same != lines.end()) ++same->quantity;
        else lines.push_back({candidate.things[t], add_ons, 1});
    }
    return lines;
}

}  // namespace

std::string describe_candidate(const Candidate& candidate) {
    std::string text;
    for (const auto& line : group_lines(candidate)) {
        if (!text.empty()) text += " + ";
        if (line.quantity > 1) text += std::to_string(line.quantity) + " x ";
        const Purchase& thing = *line.thing;
        auto named = [&](std::size_t p) {
            std::string name = part_name(thing.parts[p]);
            for (const auto& [part, add_on] : line.add_ons)
                if (part == static_cast<int>(p)) name += " with " + add_on->name;
            return name;
        };
        if (thing.kind == PurchaseKind::Single) {
            text += named(0);
            continue;
        }
        text += thing.kind == PurchaseKind::Combo ? thing.combo->name : thing.bundle->name;
        if (thing.upsize) text += ", " + thing.upsize->name;
        text += " (";
        for (std::size_t p = 0; p < thing.parts.size(); ++p) text += (p ? ", " : "") + named(p);
        text += ")";
    }
    return text;
}

nlohmann::json candidate_to_json(const Candidate& candidate) {
    using nlohmann::json;
    json out = json::array();
    for (const auto& line : group_lines(candidate)) {
        auto add_ons_on = [&](int part) {
            json list = json::array();
            for (const auto& [p, add_on] : line.add_ons)
                if (p == part) list.push_back(add_on->id);
            return list;
        };
        const Purchase& thing = *line.thing;
        json j = json::object();
        if (thing.kind == PurchaseKind::Single) {
            j["item"] = thing.item->id;
            j["size"] = thing.parts.front().size->size;
            j["quantity"] = line.quantity;
            if (auto list = add_ons_on(0); !list.empty()) j["add_ons"] = list;
        } else {
            if (thing.kind == PurchaseKind::Combo) {
                j["combo"] = thing.combo->id;
                j["upsize"] = thing.upsize ? json(thing.upsize->name) : json(nullptr);
            } else {
                j["bundle"] = thing.bundle->id;
            }
            j["quantity"] = line.quantity;
            json contents = json::array();
            for (std::size_t p = 0; p < thing.parts.size(); ++p) {
                const Part& part = thing.parts[p];
                json c = {{"role", part.role}, {"item", part.item->id}, {"size", part.size->size}};
                if (auto list = add_ons_on(static_cast<int>(p)); !list.empty()) c["add_ons"] = list;
                contents.push_back(std::move(c));
            }
            j["contents"] = std::move(contents);
        }
        out.push_back(std::move(j));
    }
    return out;
}

}  // namespace combobite
