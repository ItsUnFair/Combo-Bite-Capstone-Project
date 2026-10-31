# Target format (version 1)

A **target** is what the customer wants, in a fixed structure. The Python
front end builds one from the customer's request (with the LLM, or from the
step-by-step questions when offline), checks it, and sends it to the C++
engine, which searches for the order. The same format is used in the test
files (`tests/target_examples.json`, `tests/parser_samples.json`, and the
optimizer cases in task 8.1).

Both sides implement it:

- Python: `frontend/combobite/target.py` (`Target`, `WantedItem`)
- C++: `engine/include/combobite/target.hpp` (`Target`, `WantedItem`)

## Example

"Two cheeseburgers and a large drink, under $10, at McDonald's":

```json
{
  "format_version": 1,
  "chain": "mcdonalds",
  "goal": "cheapest",
  "max_price_cents": 1000,
  "min_calories": null,
  "max_calories": null,
  "items": [
    { "item": "cheeseburger", "quantity": 2, "size": null, "add_ons": [] },
    { "category": "drink", "quantity": 1, "size": "large", "add_ons": [] }
  ],
  "unsupported": []
}
```

## Fields

| Field | Type | Meaning |
|---|---|---|
| `format_version` | integer | Always `1`. Bump it if the format ever changes in a way old files can't follow. |
| `chain` | string or `null` | The chain's `id` from its data file (`"mcdonalds"`). `null` means the request didn't name one, so the front end asks. |
| `goal` | `"cheapest"` or `"most_calories"` | What to optimize. `"most_calories"` is for budget-only requests such as "what can I get for $6?". Price breaks ties. |
| `max_price_cents` | integer or `null` | Budget in cents. `null` means no budget. |
| `min_calories` | integer or `null` | Lowest calorie total allowed. |
| `max_calories` | integer or `null` | Highest calorie total allowed. |
| `items` | list | What the customer wants in the order (see below). May be empty for budget-only or calorie-only requests. |
| `unsupported` | list of strings | Parts of the request ComboBite can't act on, in the customer's words (`"vegetarian"`, `"no lettuce"`). They're reported back, never silently dropped. |

Every field is always written. When reading, fields that are left out count as
`null` or an empty list, except `format_version`, which is required.

## Wanted items

Each entry names **either** an exact item **or** a category, never both:

| Field | Type | Meaning |
|---|---|---|
| `item` | string | An item `id` from the chain's data file, when the customer named a specific item ("a Big Mac"). |
| `category` | string | A category from the chain's data file, for general wording ("a burger", "a drink"). |
| `quantity` | integer, 1 or more | How many. Defaults to 1. |
| `size` | string or `null` | A size name such as `"large"` or `"10 pc"`. `null` means any size. |
| `add_ons` | list of strings | Add-on ids, such as `["add_bacon"]`. |

## What reading a target checks, and what it doesn't

Reading a target (`Target.from_dict` in Python, `from_json` in C++) checks the
**shape**: the right fields with the right types, quantity of at least 1, and
exactly one of `item` or `category`. A wrong shape raises `TargetFormatError`
with the field's location, such as `items[1].quantity`.

The **meaning** is checked afterwards by the reply checks (task 2.4), using
the loaded menu data:

- the chain exists, and each item, category, size and add-on is on its menu
- no negative budget, and `min_calories` isn't above `max_calories`
- a request with a budget but no items and no calorie limits becomes
  `goal: "most_calories"`

## LLM replies

The LLM is asked to reply in this same format. Its reply can still be
wrong in ways a checked target can't be, such as a chain ComboBite doesn't
cover, several chains at once, or an item that isn't on the menu. The reply
checks catch those before a `Target` ever reaches the engine.
