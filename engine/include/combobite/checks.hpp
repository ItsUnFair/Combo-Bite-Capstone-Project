// Meaning checks on a chain whose file has the right shape (task 6.4).
//
// Errors (the chain doesn't load):
//   - a combo, bundle or add-on uses an item, size or category the file doesn't define
//   - two items, combos, bundles or add-ons share an id, or two items share a name
//   - a missing, zero or negative price; negative calories
//   - an item category that isn't in "categories"
//   - a slot with no choices or a count below 1, two slots with the same role
//   - an upsize that resizes a slot no choice can be resized for
//   - a "prices_checked" date that isn't a real YYYY-MM-DD date
// Warnings (the chain still loads):
//   - an item size with no calorie data
//   - a combo or bundle that never costs less than its items bought separately
//   - prices last checked more than 90 days ago, or a date in the future
//   - an add-on that applies to nothing
#pragma once

#include <string>
#include <vector>

#include "combobite/dates.hpp"
#include "combobite/loader.hpp"
#include "combobite/menu.hpp"

namespace combobite {

inline constexpr int kStalePriceDays = 90;

std::vector<Issue> check_chain(const Chain& chain, const Date& today);

// 449 -> "$4.49", -50 -> "-$0.50"
std::string format_money(Cents cents);

}  // namespace combobite
