// Pricing orders (task 3.2). See pricing.hpp and docs/optimizer-rules.md.
#include "combobite/pricing.hpp"

#include <algorithm>
#include <stdexcept>

namespace combobite {

namespace {

const AddOn* find_add_on(const Chain& chain, const std::string& id) {
    for (const auto& a : chain.add_ons)
        if (a.id == id) return &a;
    return nullptr;
}

}  // namespace

PurchasePrice price_purchase(const Purchase& purchase) {
    PurchasePrice price;
    switch (purchase.kind) {
        case PurchaseKind::Single:
            price.price_cents = purchase.parts.front().size->price_cents;
            break;
        case PurchaseKind::Combo:
            price.price_cents = purchase.combo->price_cents + (purchase.upsize ? purchase.upsize->extra_cents : 0);
            break;
        case PurchaseKind::Bundle:
            price.price_cents = purchase.bundle->price_cents;
            break;
    }
    for (const Part& part : purchase.parts) {
        // A single item's one part has no extra charge, so this only adds
        // the charges for combo and bundle choices.
        price.price_cents += part.extra_cents;
        price.separately_cents += part.size->price_cents;
        if (part.size->calories) price.calories += *part.size->calories;
        else price.calories_complete = false;
    }
    return price;
}

PricedOrder price_order(const Candidate& candidate) {
    PricedOrder order;
    order.things = static_cast<int>(candidate.things.size());
    for (const Purchase* thing : candidate.things) {
        const PurchasePrice p = price_purchase(*thing);
        order.total_cents += p.price_cents;
        order.separately_cents += p.separately_cents;
        order.calories += p.calories;
        order.calories_complete = order.calories_complete && p.calories_complete;
    }
    // Add-ons cost the same whether the item is in a combo or bought alone.
    for (const AddOnPlacement& placed : candidate.add_ons) {
        order.add_ons_cents += placed.add_on->price_cents;
        if (placed.add_on->calories) order.calories += *placed.add_on->calories;
        else order.calories_complete = false;
    }
    order.total_cents += order.add_ons_cents;
    order.separately_cents += order.add_ons_cents;
    return order;
}

Cents requested_add_ons_cents(const Chain& chain, const Target& target) {
    Cents total = 0;
    for (const auto& wanted : target.items)
        for (const auto& id : wanted.add_ons) {
            const AddOn* add_on = find_add_on(chain, id);
            if (!add_on)
                throw std::invalid_argument("the target asks for add-on '" + id + "', which " + chain.name +
                                            " doesn't have");
            total += add_on->price_cents * wanted.quantity;
        }
    return total;
}

}  // namespace combobite
