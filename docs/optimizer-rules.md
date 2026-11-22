# Optimizer rules

These are the rules the optimizer (tasks 3.1 to 3.6) follows when it builds,
prices and picks orders. The hand-calculated test cases in
[`tests/optimizer_cases.json`](../tests/optimizer_cases.json) (task 8.1) were
worked out with exactly these rules, so the code and the cases have to agree
with this page. If a rule changes, change it here, redo the affected cases,
and then change the code.

## What an order is made of

An order is a list of **things to order**, each one of:

- a **single item** in one of its sizes
- a **combo**, with one choice per slot, made large or not (`upsizes`)
- a **bundle**, with one choice per slot (a slot with `count: 2` gets two
  choices, which may be the same)

The same combo, bundle or item can appear more than once. Each one counts as
one thing to order, so two Big Mac Meals are two things.

## Prices

All money is in whole cents.

- Single item: the price of that size.
- Combo: the combo's `price_cents`, plus the upsize's `extra_cents` if it's
  made large, plus the `extra_cents` of every slot choice picked.
- Bundle: the bundle's `price_cents` plus the `extra_cents` of every choice.
- Add-ons: each add-on's `price_cents`, once per item it's on.

## Upsizes

Making a combo large changes the size of each slot named in the upsize's
`resize`, as long as the chosen item comes in that size. A choice that doesn't
come in it (apple slices) stays as it is. Slots that aren't named don't change.

## Meeting the target

An order meets the target when all of these hold:

1. **Every wanted item is covered.** Each unit the target asks for (a
   quantity of 2 is two units) is matched to a *different* item in the order,
   whether it's a single item or part of a combo or bundle. The item has to be
   the named item (or in the named category), and the requested size when one
   is given. A size of `null` means any size.
2. **Extras are allowed.** An order may contain items nobody asked for, such
   as the drink in a combo. They count toward the price and the calories.
3. **Add-ons go only where asked.** A wanted item with add-ons puts them on
   the item that covers it, including an item inside a combo or bundle. The
   add-on has to apply to that item. The optimizer never adds unrequested
   add-ons, even for a budget-only target.
4. **Within the budget**, when there is one (`total <= max_price_cents`).
5. **Within the calorie range**, when there is one. Calories count every item
   in the order, extras included, plus add-ons.
6. **At most 10 things to order.**

A target that asks for more than 10 units in total is refused before the
search starts (`too_many_items`), since it can't fit in 10 things.

## Items with no calorie data

A target **uses calories** when it has a `min_calories`, a `max_calories`, or
the goal `most_calories`.

- If it does, any item size with no calorie data is left out: as a single item
  and as a choice inside combos and bundles. A combo or bundle can still be
  used with its other choices. The number left out is reported.
- If it doesn't, those items are allowed and the calorie total only counts the
  known values; it's marked incomplete.

## Picking the winner

- Goal `cheapest`: the lowest total wins.
- Goal `most_calories`: the most calories wins, and the lower total breaks
  ties.
- Still tied: fewer things to order wins, and the other order is recorded as
  an equal-price alternative.
- No order meets the target: the result is `no_valid_order` (task 3.6 then
  looks for the nearest alternatives).

## What the same items cost one at a time

For the "Why this order" explanation, the optimizer also reports what the
winning order's items would cost bought as single items: every item in the
order (combo and bundle parts included) at the same size, plus the same
add-ons. In the test cases this is `separately_cents`.

## Test case format

`tests/optimizer_cases.json` has:

| Field | Meaning |
|---|---|
| `data_folders` | Folders the chain files are loaded from. `data/sample` holds the made-up sample chain, whose prices never change, so its cases test the rules themselves. |
| `checks` | The tricky parts of combo pricing the cases cover, with a description of each. |
| `cases` | The cases. |

Each case:

| Field | Meaning |
|---|---|
| `id` | Short unique name. |
| `request` | What a customer might have typed. Only for reading; the target is what's tested. |
| `checks` | Which of the `checks` it covers. |
| `target` | The target, in the [target format](target-format.md). |
| `expected` | `result` is `order`, `no_valid_order` or `too_many_items`. For an order: `total_cents`, `calories` (known calories only), `calories_complete`, `separately_cents`, `order`, and any `equal_price_alternatives`. |
| `hand_calculation` | The working, step by step. |

Lines in `order`:

```json
{ "item": "fries", "size": "large", "quantity": 1 }
{ "item": "cheeseburger", "size": "regular", "quantity": 1, "add_ons": ["add_cheese", "add_cheese"] }
{ "combo": "big_mac_meal", "upsize": "large", "quantity": 1, "contents": [
    { "role": "main", "item": "big_mac", "size": "regular" },
    { "role": "side", "item": "fries", "size": "large" },
    { "role": "drink", "item": "coca_cola", "size": "large" } ] }
{ "bundle": "two_for_six", "quantity": 1, "contents": [
    { "role": "pick", "item": "big_mac", "size": "regular" },
    { "role": "pick", "item": "big_mac", "size": "regular" } ] }
```

A combo or bundle lists every part with its size after any upsize. An add-on
on a part goes on that part (`"add_ons": ["add_bacon"]`).

A case **passes** (task 8.2) when the optimizer's result matches `result`,
its total matches `total_cents` (and its calories match `calories` for a
`most_calories` goal), and its order meets every part of the target. The items
may differ from `order` when two orders tie.

## When prices change

The cases for real chains use the prices in `data/chains/`. After a price
update, `pytest` re-adds every expected order from the current files
(`frontend/tests/test_optimizer_cases.py`) and lists each case whose total no
longer matches, with the old and new totals. Redo those hand calculations,
because a price change can also change which order is cheapest. The
`sample_burger` cases never need this.
