# Price refresh test fixtures

These files are **made-up menu rows** in the shape each price source documents
(field names from the Apify actor pages). The store, items and prices are
invented for tests; they are not real store data.

- `burger_king.json`: rows with `variants` (sizes and meals as variants of one item)
- `taco_bell.json`: flat rows, one per item, with `storeNumber`

To test against a real store's menu, run
`combobite --refresh-prices --near <ZIP> --dry-run --save-raw raw-menus`
and then `combobite --refresh-prices --from-raw raw-menus --dry-run`.
