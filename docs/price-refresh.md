# Refreshing prices from a store's online menu

`combobite --refresh-prices` pulls current prices for one restaurant per chain
and saves them into the chain files in `data/chains/`. ComboBite itself never
needs this to run: the optimizer always reads the saved files, so ordering
works offline and keeps working when the price source is down.

## What it uses

No chain publishes a price API. Each franchise sets its own prices, and they
only appear in that restaurant's online ordering menu. The refresh uses
[Apify](https://apify.com), a service with a documented API that runs
community-maintained "actors". Each actor reads one restaurant's own online
menu and returns it as rows of data.

| Chain | Price source | Notes |
|---|---|---|
| Burger King | `memo23/burger-king-menu-scraper` | Sizes and meals come as variants of an item |
| Wendy's | `memo23/wendys-menu-scraper` | |
| Taco Bell | `memo23/taco-bell-menu-scraper` | Needs Apify's residential proxy, billed from the same credits |
| McDonald's | none | Its store prices are only in its app, so its prices stay hand-entered |

The sources are listed in [`data/price_sources.json`](../data/price_sources.json).
These actors are not made or endorsed by the chains. They can stop working
when a chain changes its website, which is why every step below is built to
fail safely. Check that this use fits your course's rules and each site's
terms before relying on it.

## One-time setup

1. Create a free account at apify.com. The free plan needs no credit card and
   includes $5 of usage each month; one refresh of all three chains costs
   roughly 35 to 40 cents. Leave the card field empty and nothing can be charged.
2. In the Apify console, open **Settings → API & Integrations** and copy your
   API token.
3. Save the token in an environment variable named `APIFY_TOKEN`:
   - Windows: `setx APIFY_TOKEN "paste-your-token-here"`, then open a new terminal
   - macOS / Linux: `export APIFY_TOKEN="paste-your-token-here"`

Treat the token like a password. Never put it in a file in this folder, a
screenshot or a commit. ComboBite sends it only to Apify, in a request
header, and never prints it.

## Using it

```
combobite --refresh-prices --near 60446 --dry-run     (show what would change)
combobite --refresh-prices --near 60446               (check, then save)
```

`--near` takes a ZIP code, a city or a street address, and each chain's
nearest restaurant is used. That text is sent to Apify to find the store.

| Option | What it does |
|---|---|
| `--dry-run` | Shows the changes without saving anything |
| `--chain burger_king` | Only that chain; can be repeated |
| `--allow-big-changes` | Applies prices that moved by more than 50% |
| `--save-raw FOLDER` | Also saves each store's menu exactly as received |
| `--from-raw FOLDER` | Uses menus saved earlier instead of the internet (no token needed) |

## How it stays reliable

1. **Pull.** One request per chain. If it fails (no connection, bad token, no
   credits, timeout, unexpected reply), that chain is reported and its saved
   prices are kept. The other chains still run.
2. **Match.** Each price in our file is matched to a menu row by name,
   ignoring case, punctuation and trademark signs. Anything that can't be
   matched keeps its old price and is listed as `no match`.
3. **Guard.** A price that moved by more than 50% is `held`, not applied,
   because that is far more likely a wrong match than a real change.
4. **Check.** The updated file must pass the same checks as
   `combobite --check-data`. If it has errors, nothing is saved.
5. **Save.** Only then is the chain file replaced, with today's date and the
   store's number and address in `prices_checked`.

The exit code is 0 when every chain that was tried is fine, 1 when at least
one chain failed and kept its old prices, and 2 when it couldn't start (no
token, no `--near`, or the engine isn't built).

## Reading the report

```
Burger King
  store      #12427, 801 3Rd Avenue W, Birmingham, Alabama
  updated    Whopper: $7.59 -> $5.89
  held       Bacon King: $12.49 -> $1.99 is a change of more than 50% ...
  no match   Whopper Meal: kept $11.49; add its menu name under "combo:whopper_meal" ...
  4 updated, 6 already correct, 1 held, 7 not matched (of 18 prices)
  Saved.
```

**Fixing a `no match`.** Run with `--save-raw raw-menus`, open the saved file
and find what the store calls the item. Then add that name in
`data/price_sources.json`:

- under `item_names` for an item (sizes are tried automatically, as
  `Medium French Fries` and `French Fries | Medium`), or
- under `aliases` for one exact entry, for example
  `"combo:whopper_meal": ["Whopper | Medium - Side and Drink Included"]`.
  A variant of a menu item is written `Item name | Variant name`.

Run again with `--from-raw raw-menus --dry-run` to test the change for free.

## What it doesn't do

- **It only changes prices.** Items, sizes, calories, combo contents and deals
  are still maintained by hand (see [data-format.md](data-format.md)).
- **Combos and bundles need aliases** before they refresh, because stores name
  them differently from our files. Until then they keep their old prices,
  and the data check may warn that a combo no longer beats its parts.
- **Upsize and substitution charges** aren't refreshed.
- **When only some prices match,** `prices_checked` says how many came from
  the store, since the rest are older.
- **The item names in `price_sources.json` are first guesses** until they've
  been checked against a real store's menu.
