# Chain data file format (version 1)

Each chain ComboBite covers has one JSON file in `data/chains/`, named after
the chain's id (for example `data/chains/mcdonalds.json`). The C++ engine
loads these files; the structs in `engine/include/combobite/menu.hpp`
mirror this format field for field.

A complete example that uses every feature is in
[`data/sample/sample_chain.json`](../data/sample/sample_chain.json). Its chain
and prices are made up, so copy it as a starting point but don't use its
numbers.

## Rules that apply everywhere

- **Money is in whole cents.** `$4.49` is written `449`. Never use decimals,
  so totals never round wrong.
- **Calories are optional.** Write `null` (or leave the field out) when the
  chain doesn't publish them. The item still works for price-only requests.
- **Things are referred to by `id`.** Ids are lowercase with underscores
  (`classic_burger`) and must be unique within the file. Names are what the
  customer says at the counter (`Classic Burger`).
- **Sizes are named per item.** Use `"regular"` for single-size items.
  Use the chain's own words for the rest (`"small"`, `"medium"`, `"4 pc"`).
  When a combo or bundle refers to a single-size item, its size can be left out.

## Top level

| Field | Required | Meaning |
|---|---|---|
| `format_version` | yes | Always `1` for this version of the format. |
| `chain` | yes | `id`, `name` and `aliases` (other names customers use, such as `"McD"`). |
| `prices_checked` | yes | `date` (`YYYY-MM-DD`) and `store` the prices came from. |
| `categories` | yes | Every category used by an item, e.g. `["burger", "chicken", "side", "drink"]`. |
| `items` | yes | Everything sold on its own. |
| `add_ons` | no | Extras like cheese or bacon. |
| `combos` | no | Main + side + drink style meals at one price. |
| `bundles` | no | Mix-and-match deals and fixed-price boxes. |
| `unsupported_deals` | no | Notes about real deals the format can't express. |

## Items

```json
{ "id": "fries", "name": "Fries", "category": "side",
  "sizes": [
    { "size": "small",  "price_cents": 199, "calories": 230 },
    { "size": "medium", "price_cents": 309, "calories": 320 } ] }
```

Each entry in `sizes` is one way to buy the item, with its own price and
calories. Items sold by piece count (nuggets, wings) are one item whose
sizes are the counts.

## Add-ons

```json
{ "id": "add_bacon", "name": "Add Bacon", "price_cents": 129, "calories": 90,
  "applies_to_categories": ["burger"],
  "applies_to_items": ["chicken_sandwich"],
  "max_per_item": 1 }
```

An add-on can go on any item in `applies_to_categories` or listed in
`applies_to_items` (either list may be left out). `max_per_item` defaults
to 1.

## Slots: the parts of a combo or bundle

Combos and bundles are both built from **slots**. A slot is one part of the
deal, such as "the side" or "pick 2 sandwiches".

```json
{ "role": "side", "count": 1, "choices": [
    { "item": "fries", "size": "medium" },
    { "item": "apple_slices" },
    { "item": "side_salad", "extra_cents": 100 } ] }
```

- `role` is a label used in explanations (`main`, `side`, `drink`, `pick`).
- `count` is how many of this slot the deal includes (default 1).
- `choices` lists what the slot can be filled with. `extra_cents` is the
  charge for picking that choice (default 0).
- **In a combo, the first choice is what comes with it.** The others are
  substitutions.

## Combos

```json
{ "id": "classic_burger_combo", "name": "Classic Burger Combo",
  "price_cents": 849, "base_size": "medium",
  "slots": [ ...main, side, drink slots... ],
  "upsizes": [
    { "name": "large", "extra_cents": 120,
      "resize": { "side": "large", "drink": "large" } } ] }
```

`base_size` names the normal combo size. Each upsize adds `extra_cents` to
the combo price and changes the size of the slots listed in `resize`. A
slot choice that doesn't come in the new size (apple slices, say) stays as
it is.

## Bundles

```json
{ "id": "two_for_six", "name": "2 for $6 Mix & Match", "price_cents": 600,
  "slots": [ { "role": "pick", "count": 2, "choices": [
      { "item": "classic_burger" }, { "item": "chicken_sandwich" } ] } ] }
```

A bundle has a fixed price and one or more slots. A mix-and-match deal is
one slot with `count` 2 or more. A fixed box or bag is one slot per part.

## How common real deals fit

These are the kinds of deals the big chains run. Check your chosen chains'
current menus against this list while collecting data (task 6.2).

| Deal on the menu | How to write it |
|---|---|
| Value meal: main + medium side + medium drink, "make it large" for a charge | Combo with `base_size: "medium"` and an upsize |
| Swap the side or drink, sometimes for a charge | More `choices` in that slot, with `extra_cents` |
| Meal where you pick one of several mains | Combo whose `main` slot lists several choices |
| Fixed-price box or bag with several items ($5 meal deal, value bags, boxes) | Bundle with one slot per part |
| Choose a main inside a box, with an upcharge for premium ones | That slot's choices, with `extra_cents` on the premium ones |
| "2 for $X" or "any 2 for $X" mix and match | Bundle with one slot, `count: 2` |
| Nuggets or wings by piece count | One item with sizes like `"4 pc"`, `"10 pc"` |
| Add cheese, add bacon | Add-ons |

**Not supported** (write them in `unsupported_deals` instead): app-only or
coupon offers, deals that need a loyalty account, BOGO offers, "free with a
purchase over $X", time-of-day menus such as breakfast hours, and limits on
how many of a deal one order can have.
