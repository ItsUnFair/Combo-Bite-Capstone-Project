"""Refreshing chain prices from a store's online menu (`combobite --refresh-prices`).

How it stays reliable (see docs/price-refresh.md):
  1. Pull: one Apify actor run per chain returns one restaurant's menu rows.
  2. Match: each of our priced entries is matched to a menu row by name.
     Anything that can't be matched is reported and keeps its old price.
  3. Guard: a price that moved by more than 50% is held back unless allowed.
  4. Check: the updated file must pass the engine's --check-data.
  5. Save: only then is data/chains/<chain>.json replaced.
If any step fails, the saved file is left exactly as it was, so ComboBite
keeps working from the last good prices. The optimizer never calls the API.

The API token is read from the APIFY_TOKEN environment variable and sent
only in the Authorization header, never in a URL and never printed.
"""

from __future__ import annotations

import copy
import datetime
import json
import os
import re
import tempfile
import urllib.error
import urllib.request
from dataclasses import dataclass, field
from pathlib import Path
from typing import Callable

from combobite import engine

SOURCES_FILE = engine.PROJECT_ROOT / "data" / "price_sources.json"
TOKEN_ENV_VAR = "APIFY_TOKEN"
API_URL = "https://api.apify.com/v2/actors/{actor}/run-sync-get-dataset-items"
REQUEST_TIMEOUT_SECONDS = 330  # the API itself gives up after 300
BIG_CHANGE = 0.50              # a price moving more than this is held back


class PriceSourceError(Exception):
    """The price source couldn't be reached or gave something unusable."""


# ------------------------------------------------------------------ fetch ---

def fetch_rows(actor: str, run_input: dict, token: str) -> list[dict]:
    """Runs one Apify actor and returns its rows. Raises PriceSourceError with
    a message a person can act on."""
    request = urllib.request.Request(
        API_URL.format(actor=actor),
        data=json.dumps(run_input).encode("utf-8"),
        headers={"Content-Type": "application/json", "Authorization": f"Bearer {token}"},
        method="POST",
    )
    try:
        with urllib.request.urlopen(request, timeout=REQUEST_TIMEOUT_SECONDS) as response:
            body = response.read().decode("utf-8")
    except urllib.error.HTTPError as error:
        reasons = {
            401: f"Apify rejected the API token. Check the {TOKEN_ENV_VAR} environment variable.",
            402: "the Apify account is out of credits for this month.",
            403: "the Apify account isn't allowed to run this actor.",
            404: f"the actor '{actor}' wasn't found on Apify; it may have been renamed or removed.",
            408: "the price source took longer than 5 minutes and was stopped.",
            429: "Apify is limiting requests right now; try again in a minute.",
        }
        detail = reasons.get(error.code, f"Apify answered with HTTP {error.code}.")
        raise PriceSourceError(detail) from error
    except (urllib.error.URLError, TimeoutError, OSError) as error:
        raise PriceSourceError(f"couldn't reach Apify ({getattr(error, 'reason', error)}).") from error
    try:
        rows = json.loads(body)
    except json.JSONDecodeError as error:
        raise PriceSourceError("the price source's reply wasn't valid JSON.") from error
    if not isinstance(rows, list):
        raise PriceSourceError("the price source's reply wasn't a list of menu rows.")
    return rows


# ------------------------------------------------------------------ match ---

def normalize(name: str) -> str:
    """'Whopper Jr.®' and 'whopper jr' compare equal; so do '4 Pc.' and '4 piece'."""
    text = name.lower().replace("&", " and ")
    text = re.sub(r"[®™©]", "", text)
    text = re.sub(r"\bpieces?\b", "pc", text)
    text = re.sub(r"[^a-z0-9|]+", " ", text).replace("|", " | ")
    return re.sub(r"\s+", " ", text).strip()


@dataclass
class StoreInfo:
    number: str = ""
    address: str = ""
    city: str = ""
    state: str = ""

    def describe(self) -> str:
        parts = [p for p in (self.address, self.city, self.state) if p]
        number = f"#{self.number}" if self.number else "store"
        return f"{number}, {', '.join(parts)}" if parts else number


def read_store(rows: list[dict]) -> StoreInfo:
    for row in rows:
        number = row.get("storeNumber") or row.get("storeId") or row.get("restaurantId")
        if number or row.get("storeAddress"):
            return StoreInfo(
                number=str(number or ""),
                address=str(row.get("storeAddress") or "").title(),
                city=str(row.get("storeCity") or "").title(),
                state=str(row.get("storeState") or ""),
            )
    return StoreInfo()


def to_cents(value) -> int | None:
    if isinstance(value, bool) or not isinstance(value, (int, float)):
        return None
    cents = round(value * 100)
    return cents if cents > 0 else None


def build_offers(rows: list[dict]) -> tuple[dict[str, int], set[str]]:
    """Turns menu rows into {normalized name: price in cents}.

    A row's variants become 'Item name | Variant name'. A name that appears
    with two different prices is ambiguous and is returned separately."""
    offers: dict[str, int] = {}
    ambiguous: set[str] = set()

    def add(name: str, price) -> None:
        cents = to_cents(price)
        key = normalize(name)
        if cents is None or not key:
            return
        if key in offers and offers[key] != cents:
            ambiguous.add(key)
        offers.setdefault(key, cents)

    for row in rows:
        if not isinstance(row, dict) or row.get("isAvailable") is False:
            continue
        name = row.get("itemName")
        if not isinstance(name, str):
            continue
        add(name, row.get("price"))
        for variant in row.get("variants") or []:
            if isinstance(variant, dict) and isinstance(variant.get("name"), str):
                add(f"{name} | {variant['name']}", variant.get("pickupPrice", variant.get("price")))
    for key in ambiguous:
        offers.pop(key, None)
    return offers, ambiguous


@dataclass
class Entry:
    """One price in a chain file, and how to read and write it."""

    key: str            # 'fries/medium', 'combo:whopper_meal', ...
    label: str          # 'Classic Fries (medium)'
    candidates: list[str]
    holder: dict        # the dict that has "price_cents"


def priced_entries(chain: dict, source: dict) -> list[Entry]:
    item_names = source.get("item_names", {})
    aliases = source.get("aliases", {})
    entries: list[Entry] = []

    for item in chain.get("items", []):
        names = [item["name"], *item_names.get(item["id"], [])]
        single = len(item["sizes"]) == 1
        for size in item["sizes"]:
            key = f"{item['id']}/{size['size']}"
            candidates = list(aliases.get(key, []))
            # The bare name is only safe for a one-size item sold as "regular".
            # If our file lists just "medium", the menu's plain "French Fries"
            # row could be any size, so only size-specific names may match.
            bare_ok = single and size["size"] == "regular"
            for name in names:
                if bare_ok:
                    candidates.append(name)
                else:
                    candidates += [f"{size['size']} {name}", f"{name} | {size['size']}", f"{name} {size['size']}"]
            label = item["name"] if bare_ok else f"{item['name']} ({size['size']})"
            entries.append(Entry(key, label, candidates, size))

    for kind, field_name in (("add_on", "add_ons"), ("combo", "combos"), ("bundle", "bundles")):
        for thing in chain.get(field_name, []):
            key = f"{kind}:{thing['id']}"
            entries.append(Entry(key, thing["name"], [*aliases.get(key, []), thing["name"]], thing))
    return entries


# ----------------------------------------------------------------- refresh --

@dataclass
class Change:
    entry: Entry
    status: str                 # updated | unchanged | no match | ambiguous | held
    old_cents: int
    new_cents: int | None = None
    matched_name: str = ""


@dataclass
class ChainResult:
    chain_id: str
    chain_name: str
    ok: bool = True
    saved: bool = False
    message: str = ""                       # why it failed or wasn't attempted
    store: StoreInfo = field(default_factory=StoreInfo)
    changes: list[Change] = field(default_factory=list)
    check_output: str = ""

    def count(self, *statuses: str) -> int:
        return sum(1 for change in self.changes if change.status in statuses)


def money(cents: int) -> str:
    return f"${cents / 100:.2f}"


def plan_changes(chain: dict, source: dict, rows: list[dict], allow_big: bool) -> list[Change]:
    offers, ambiguous = build_offers(rows)
    changes = []
    for entry in priced_entries(chain, source):
        old = entry.holder["price_cents"]
        keys = [normalize(candidate) for candidate in entry.candidates]
        hit = next((k for k in keys if k in offers), None)
        if hit is None:
            status = "ambiguous" if any(k in ambiguous for k in keys) else "no match"
            changes.append(Change(entry, status, old))
            continue
        new = offers[hit]
        if new == old:
            status = "unchanged"
        elif not allow_big and old > 0 and abs(new - old) / old > BIG_CHANGE:
            status = "held"
        else:
            status = "updated"
        changes.append(Change(entry, status, old, new, hit))
    return changes


def refresh_chain(
    path: Path,
    source: dict,
    rows: list[dict],
    *,
    today: datetime.date,
    dry_run: bool = False,
    allow_big: bool = False,
    check: Callable[[Path], tuple[int, str]] | None = None,
) -> ChainResult:
    """Applies one store's rows to one chain file. Never leaves the file half-written."""
    original = json.loads(path.read_text(encoding="utf-8"))
    chain = copy.deepcopy(original)
    result = ChainResult(chain["chain"]["id"], chain["chain"]["name"])

    if not rows:
        result.ok = False
        result.message = "the store's menu came back empty, so nothing was changed."
        return result

    result.store = read_store(rows)
    result.changes = plan_changes(chain, source, rows, allow_big)
    matched = result.count("updated", "unchanged")
    if matched == 0:
        result.ok = False
        result.message = "none of our items could be matched to the store's menu, so nothing was changed."
        return result

    for change in result.changes:
        if change.status == "updated":
            change.entry.holder["price_cents"] = change.new_cents
    total = len(result.changes)
    store = f"{chain['chain']['name']} {result.store.describe()}; pickup prices from the store's online menu via Apify"
    if matched < total:
        store += f". {matched} of {total} prices are from this store; the rest are older estimates"
    chain["prices_checked"] = {"date": today.isoformat(), "store": store}

    if dry_run:
        return result

    check = check or engine.check_data_quiet
    with tempfile.TemporaryDirectory() as folder:
        candidate = Path(folder) / path.name
        candidate.write_text(json.dumps(chain, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
        code, output = check(Path(folder))
        result.check_output = output
        if code != 0:
            result.ok = False
            result.message = "the refreshed file failed the data check, so the old prices were kept."
            return result
        path.write_text(candidate.read_text(encoding="utf-8"), encoding="utf-8")
    result.saved = True
    return result


# ------------------------------------------------------------------ report --

def print_result(result: ChainResult, dry_run: bool) -> None:
    print(f"\n{result.chain_name}")
    if result.store.number or result.store.address:
        print(f"  store      {result.store.describe()}")
    order = ["updated", "held", "no match", "ambiguous"]
    for status in order:
        for change in (c for c in result.changes if c.status == status):
            label = change.entry.label
            if status == "updated":
                print(f"  updated    {label}: {money(change.old_cents)} -> {money(change.new_cents)}")
            elif status == "held":
                print(f"  held       {label}: {money(change.old_cents)} -> {money(change.new_cents)} is a change of more "
                      f"than {int(BIG_CHANGE * 100)}%, so it wasn't applied (use --allow-big-changes if it's right)")
            elif status == "no match":
                print(f"  no match   {label}: kept {money(change.old_cents)}; add its menu name under "
                      f"\"{change.entry.key}\" in data/price_sources.json")
            else:
                print(f"  ambiguous  {label}: the store lists this name at more than one price; kept {money(change.old_cents)}")
    if result.changes:
        print(f"  {result.count('updated')} updated, {result.count('unchanged')} already correct, "
              f"{result.count('held')} held, {result.count('no match', 'ambiguous')} not matched "
              f"(of {len(result.changes)} prices)")
    for line in result.check_output.splitlines():
        if line.startswith("  error") or line.startswith("  warning"):
            print(f"  check    {line.strip()}")
    if not result.ok:
        print(f"  NOT SAVED: {result.message}")
    elif dry_run:
        print("  Dry run: nothing was saved.")
    elif result.saved:
        print("  Saved.")


# -------------------------------------------------------------------- main --

def load_sources(path: Path = SOURCES_FILE) -> dict:
    return json.loads(path.read_text(encoding="utf-8"))["chains"]


def run_refresh(
    *,
    near: str | None,
    data_folder: Path,
    chains: list[str] | None = None,
    dry_run: bool = False,
    allow_big: bool = False,
    save_raw: Path | None = None,
    from_raw: Path | None = None,
    sources: dict | None = None,
    fetch: Callable[[str, dict, str], list[dict]] = fetch_rows,
    today: datetime.date | None = None,
) -> int:
    """Refreshes prices for every chain that has a source. Returns the exit
    code: 0 = every chain attempted is fine, 1 = at least one chain failed
    (its old prices were kept), 2 = it couldn't start."""
    sources = load_sources() if sources is None else sources
    today = today or datetime.date.today()
    token = os.environ.get(TOKEN_ENV_VAR, "")

    if from_raw is None:
        if not near:
            print("Say where to look with --near, for example: combobite --refresh-prices --near 60446")
            return 2
        if not token:
            print(f"No Apify API token found. Set the {TOKEN_ENV_VAR} environment variable; "
                  "see docs/price-refresh.md for how to get a free token.")
            return 2
    if not dry_run:
        try:
            engine.find_engine()
        except engine.EngineNotFound as problem:
            print(problem)
            return 2

    files = sorted(data_folder.glob("*.json"))
    if not files:
        print(f"No chain files were found in {data_folder}")
        return 2
    if save_raw is not None:
        save_raw.mkdir(parents=True, exist_ok=True)

    failed = False
    for path in files:
        chain = json.loads(path.read_text(encoding="utf-8"))
        chain_id, name = chain["chain"]["id"], chain["chain"]["name"]
        if chains and chain_id not in chains:
            continue
        source = sources.get(chain_id)
        if source is None:
            print(f"\n{name}\n  No price source is set up for this chain, so its hand-entered prices "
                  f"(checked {chain['prices_checked']['date']}) are unchanged.")
            continue

        try:
            if from_raw is not None:
                raw_file = from_raw / f"{chain_id}.json"
                if not raw_file.is_file():
                    print(f"\n{name}\n  No saved menu at {raw_file}; skipped.")
                    continue
                rows = json.loads(raw_file.read_text(encoding="utf-8"))
            else:
                print(f"\nAsking for the {name} menu near {near} (this can take a minute)...")
                run_input = {**source.get("input", {}), source.get("location_field", "addresses"): [near]}
                rows = fetch(source["actor"], run_input, token)
                if save_raw is not None:
                    (save_raw / f"{chain_id}.json").write_text(json.dumps(rows, indent=1), encoding="utf-8")
        except (PriceSourceError, json.JSONDecodeError) as problem:
            failed = True
            print(f"\n{name}\n  NOT SAVED: {problem} The prices checked {chain['prices_checked']['date']} were kept.")
            continue

        result = refresh_chain(path, source, rows, today=today, dry_run=dry_run, allow_big=allow_big)
        print_result(result, dry_run)
        failed = failed or not result.ok

    return 1 if failed else 0
