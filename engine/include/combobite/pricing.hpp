// Pricing orders (task 3.2).
//
// Adds up what a candidate order (task 3.1) costs and how many calories it
// has, using the chain's own rules: combo and bundle prices, upsize charges,
// charges for slot choices, and add-ons. All money is whole cents.
//
// The rules are in docs/optimizer-rules.md, under "Prices".
#pragma once

#include <string>
#include <vector>

#include "combobite/candidates.hpp"
#include "combobite/checks.hpp"  // format_money
#include "combobite/menu.hpp"
#include "combobite/target.hpp"

namespace combobite {

// What one thing to order costs and contains, without add-ons.
struct PurchasePrice {
    Cents price_cents = 0;       // what it costs at the counter
    Cents separately_cents = 0;  // its parts bought as single items, same sizes
    int calories = 0;            // known calories of its parts
    bool calories_complete = true;  // false if any part has no calorie data
};

// Single item: the size's price. Combo: the combo's price, plus the upsize
// charge if it's made large, plus each slot choice's extra charge. Bundle:
// the bundle's price plus each choice's extra charge.
PurchasePrice price_purchase(const Purchase& purchase);

// A whole candidate order, add-ons included.
struct PricedOrder {
    Cents total_cents = 0;
    Cents separately_cents = 0;  // every item in it bought one at a time, add-ons included
    Cents add_ons_cents = 0;     // the part of the total that is add-ons
    int calories = 0;            // known calories only
    bool calories_complete = true;
    int things = 0;              // things to order (a combo counts as one)

    // How much the order saves over buying the same items one at a time.
    Cents savings_cents() const { return separately_cents - total_cents; }
};

PricedOrder price_order(const Candidate& candidate);

// What every add-on the target asks for costs in total. Every candidate
// includes exactly these add-ons, so this is the same for all of them; the
// search (tasks 3.3 and 3.4) adds it to an order's cost before the add-ons
// are placed.
Cents requested_add_ons_cents(const Chain& chain, const Target& target);

}  // namespace combobite
