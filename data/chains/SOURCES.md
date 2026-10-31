# Where the chain data came from

**These prices are national-average estimates, not prices from one store.**
Chains don't publish national prices; each franchise sets its own. The figures
below come from price-tracking websites that collect prices from the chains'
apps, and those sites often disagree with each other by $0.50 to $1.50 per item.

Before relying on a file, follow the price-update checklist: pick one store in
the chain's app, update every `price_cents` to that store's prices, then set
`prices_checked.store` to that store and `prices_checked.date` to the day you
checked. Menu items, sizes, deal contents and calories can stay as they are
unless the store shows something different.

Collected October 30, 2026, for task 6.2.

## Rules used

- For each chain, one site was the main source for prices (the most recently
  updated one), and gaps were filled from the next source in the list.
- Calories come from the chain's own nutrition page where it was reachable,
  otherwise from FatSecret's US restaurant listings, otherwise from the price
  site. Where no source gave calories, the file says `null`.
- Deals whose contents or price couldn't be confirmed are listed in each
  file's `unsupported_deals` instead of being guessed.

## McDonald's (`mcdonalds.json`)

| Data | Source |
|---|---|
| Meal prices, fries, McDouble, Cheeseburger, Hamburger, McChicken, Apple Slices, $5 Meal Deal | [usafoodmenu.com](https://usafoodmenu.com/menu/mcdonalds-menu/), updated Oct 25, 2026 |
| Big Mac, Quarter Pounder with Cheese, McCrispy, McNuggets, Apple Pie | [menupricebook.com](https://menupricebook.com/menu/mcdonalds/), updated Aug 27, 2026 |
| Coca-Cola prices; 2 for $X eligible items | [menupedia.us](https://menupedia.us/mcdonalds-menu/), reviewed May 2026 |
| Large meal upcharge ($0.81) | Big Mac Large Meal minus Big Mac Meal averages at [menupricetracker.com](https://menupricetracker.com/mcdonalds-menu) |
| 2 for $6 price | menupricebook.com (menupedia says $5 nationally, $6 to $7 in higher-cost markets) |
| Calories: small and medium Coca-Cola | [mcdonalds.com](https://www.mcdonalds.com/us/en-us/product/coca-cola-small.html) |
| Calories: fries, apple slices | usafoodmenu.com |
| Calories: everything else | [FatSecret](https://foods.fatsecret.com/calories-nutrition/mcdonalds) |

## Wendy's (`wendys.json`)

| Data | Source |
|---|---|
| Combo prices, fries, soft drinks, Frosty (prices and calories) | [usafoodmenu.com](https://usafoodmenu.com/menu/wendys-menu/), updated Oct 25, 2026 |
| Sandwich and nugget prices | [menupricebook.com](https://menupricebook.com/menu/wendys/), updated Aug 25, 2026 |
| Crispy Chicken Sandwich, Double Stack prices; $6 Biggie Bag contents | [menupedia.us](https://menupedia.us/wendys-menu/), reviewed May 2026 |
| $4 Biggie Bites contents | usafoodmenu.com |
| Calories: sandwiches and nuggets | [FatSecret](https://foods.fatsecret.com/calories-nutrition/wendys) |
| Calories: Double Stack | Not found (`null`) |

## Burger King (`burger_king.json`)

| Data | Source |
|---|---|
| All prices; calories except Coca-Cola | [usafoodmenu.com](https://usafoodmenu.com/menu/burger-king-menu/), updated Oct 25, 2026 |
| $5 Your Way Meal contents | [menupedia.us](https://menupedia.us/burger-king-menu/), reviewed May 2026 |
| Calories: Coca-Cola | [FatSecret](https://foods.fatsecret.com/calories-nutrition/burger-king) |

Burger King uses a single price source on purpose. Mixing sources made two
meals cost more than their items bought separately, because the sources were
months apart. That source lists fries and drinks only in medium (and drinks in
large), so the Your Way Meal, which needs small sizes, waits for store prices.

## Taco Bell (`taco_bell.json`)

| Data | Source |
|---|---|
| Value menu items (Crunchy Taco, Soft Taco, Cheesy Bean and Rice Burrito, Cheesy Roll Up, Cinnamon Twists), drinks, combo and box prices | [usafoodmenu.com](https://usafoodmenu.com/menu/taco-bell-menu/), updated Oct 25, 2026 |
| Other item prices | [menupricebook.com](https://menupricebook.com/menu/taco-bell/), updated Sep 26, 2026 |
| Luxe Cravings Box contents | [Nation's Restaurant News](https://www.nrn.com/quick-service/taco-bell-expands-its-luxe-box-value-offerings), Jan 7, 2025 |
| Calories | [FatSecret](https://foods.fatsecret.com/calories-nutrition/taco-bell) (beef versions where several were listed); medium Pepsi from usafoodmenu.com |

Taco Bell has no combos or boxes in its file yet. Their prices are known, but
the combo sides and the Beefy 5-Layer Burrito's price weren't, so they're
listed as "TO ADD AT YOUR STORE" in `unsupported_deals`.
