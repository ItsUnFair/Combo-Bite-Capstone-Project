"""Task 8.1 checks for tests/optimizer_cases.json, the hand-calculated
optimizer cases.

These tests don't search for anything. They re-add every expected order from
the chain files, the way a spreadsheet would, and check that:

- every target has the right shape and only uses things on the menu
- every expected order is made of real menu entries (nothing "stale")
- the total, calories and one-at-a-time cost add up to what the case says
- the order really meets its target (items, sizes, add-ons, budget,
  calories, the 10-item cap)

When prices in data/chains/ change, the totals stop adding up and the
failure names the case and both totals, so the hand calculation can be
redone. The rules are in docs/optimizer-rules.md.
"""

from __future__ import annotations

import json
from collections import Counter
from pathlib import Path

import pytest

from combobite.target import Target

ROOT = Path(__file__).resolve().parents[2]
CASES_FILE = ROOT / "tests" / "optimizer_cases.json"
ITEM_CAP = 10
RESULTS = ("order", "no_valid_order", "too_many_items")

DOC = json.loads(CASES_FILE.read_text(encoding="utf-8"))
CASES = DOC["cases"]


def _load_chains() -> dict[str, dict]:
    chains = {}
    for folder in DOC["data_folders"]:
        for path in sorted((ROOT / folder).glob("*.json")):
            data = json.loads(path.read_text(encoding="utf-8"))
            chains[data["chain"]["id"]] = data
    return chains


CHAINS = _load_chains()


class Menu:
    """Look-ups into one chain file."""

    def __init__(self, chain: dict):
        self.items = {i["id"]: i for i in chain["items"]}
        self.add_ons = {a["id"]: a for a in chain.get("add_ons", [])}
        self.combos = {c["id"]: c for c in chain.get("combos", [])}
        self.bundles = {b["id"]: b for b in chain.get("bundles", [])}

    def size(self, item: str, size: str) -> dict:
        assert item in self.items, f"'{item}' isn't on the menu (stale case?)"
        for s in self.items[item]["sizes"]:
            if s["size"] == size:
                return s
        raise AssertionError(f"'{item}' has no size '{size}' (stale case?)")

    def has_size(self, item: str, size: str) -> bool:
        return any(s["size"] == size for s in self.items[item]["sizes"])

    def default_size(self, item: str) -> str:
        return self.items[item]["sizes"][0]["size"]

    def category(self, item: str) -> str:
        return self.items[item]["category"]


class Priced:
    """An expected order added up: cost, calories and the item units in it."""

    def __init__(self):
        self.total = 0
        self.separately = 0
        self.calories = 0
        self.complete = True
        self.things = 0
        self.units: list[dict] = []  # {"item", "size", "add_ons"}

    def add_unit(self, menu: Menu, item: str, size: str, add_ons: list[str]):
        entry = menu.size(item, size)
        self.separately += entry["price_cents"]
        if entry.get("calories") is None:
            self.complete = False
        else:
            self.calories += entry["calories"]
        for counts in Counter(add_ons).items():
            add_on_id, n = counts
            assert add_on_id in menu.add_ons, f"add-on '{add_on_id}' isn't on the menu"
            add_on = menu.add_ons[add_on_id]
            assert n <= add_on.get("max_per_item", 1), f"too many '{add_on_id}' on one {item}"
            applies = menu.category(item) in add_on.get("applies_to_categories", []) or \
                item in add_on.get("applies_to_items", [])
            assert applies, f"'{add_on_id}' can't go on {item}"
            self.total += add_on["price_cents"] * n
            self.separately += add_on["price_cents"] * n
            self.calories += (add_on.get("calories") or 0) * n
        self.units.append({"item": item, "size": size, "add_ons": sorted(add_ons)})


def _deal_parts(menu: Menu, deal: dict, line: dict, upsize: dict | None, name: str) -> int:
    """Checks a combo or bundle's contents against its slots; returns the
    extra charges for the choices picked."""
    contents = line["contents"]
    roles = Counter(p["role"] for p in contents)
    slot_roles = {s["role"]: s for s in deal["slots"]}
    assert set(roles) == set(slot_roles), f"{name}: parts {sorted(roles)} don't match its slots {sorted(slot_roles)}"
    extra = 0
    for role, slot in slot_roles.items():
        assert roles[role] == slot.get("count", 1), f"{name}: slot '{role}' needs {slot.get('count', 1)} part(s)"
    for part in contents:
        slot = slot_roles[part["role"]]
        found = None
        for choice in slot["choices"]:
            if choice["item"] != part["item"]:
                continue
            size = choice.get("size") or menu.default_size(choice["item"])
            if upsize and part["role"] in upsize["resize"] and choice.get("size") is not None \
                    and menu.has_size(choice["item"], upsize["resize"][part["role"]]):
                size = upsize["resize"][part["role"]]
            if size == part["size"]:
                found = choice
                break
        assert found is not None, \
            f"{name}: '{part['item']}' ({part['size']}) isn't a choice for its '{part['role']}' slot"
        extra += found.get("extra_cents", 0)
    return extra


def price_order(menu: Menu, lines: list[dict]) -> Priced:
    priced = Priced()
    for line in lines:
        quantity = line.get("quantity", 1)
        assert quantity >= 1
        for _ in range(quantity):
            priced.things += 1
            if "item" in line:
                entry = menu.size(line["item"], line["size"])
                priced.total += entry["price_cents"]
                priced.add_unit(menu, line["item"], line["size"], line.get("add_ons", []))
            elif "combo" in line:
                assert line["combo"] in menu.combos, f"combo '{line['combo']}' isn't on the menu (stale case?)"
                combo = menu.combos[line["combo"]]
                upsize = None
                if line.get("upsize"):
                    matches = [u for u in combo.get("upsizes", []) if u["name"] == line["upsize"]]
                    assert matches, f"{line['combo']} can't be made {line['upsize']}"
                    upsize = matches[0]
                priced.total += combo["price_cents"] + (upsize["extra_cents"] if upsize else 0)
                priced.total += _deal_parts(menu, combo, line, upsize, line["combo"])
                for part in line["contents"]:
                    priced.add_unit(menu, part["item"], part["size"], part.get("add_ons", []))
            elif "bundle" in line:
                assert line["bundle"] in menu.bundles, f"bundle '{line['bundle']}' isn't on the menu (stale case?)"
                bundle = menu.bundles[line["bundle"]]
                priced.total += bundle["price_cents"] + _deal_parts(menu, bundle, line, None, line["bundle"])
                for part in line["contents"]:
                    priced.add_unit(menu, part["item"], part["size"], part.get("add_ons", []))
            else:
                raise AssertionError(f"order line needs 'item', 'combo' or 'bundle': {line}")
    return priced


def wanted_units(target: dict) -> list[dict]:
    units = []
    for wanted in target["items"]:
        units += [wanted] * wanted.get("quantity", 1)
    return units


def uses_calories(target: dict) -> bool:
    return target["min_calories"] is not None or target["max_calories"] is not None \
        or target["goal"] == "most_calories"


def covers(menu: Menu, target: dict, units: list[dict]) -> bool:
    """Can every wanted unit be matched to a different item in the order?"""
    wanted = wanted_units(target)

    def fits(w, u):
        if w.get("item") and u["item"] != w["item"]:
            return False
        if w.get("category") and menu.category(u["item"]) != w["category"]:
            return False
        if w.get("size") and u["size"] != w["size"]:
            return False
        return sorted(w.get("add_ons", [])) == u["add_ons"]

    taken: dict[int, int] = {}

    def place(i, seen):
        for j, unit in enumerate(units):
            if j in seen or not fits(wanted[i], unit):
                continue
            seen.add(j)
            if j not in taken or place(taken[j], seen):
                taken[j] = i
                return True
        return False

    return all(place(i, set()) for i in range(len(wanted)))


def check_meets_target(menu: Menu, target: dict, priced: Priced):
    assert priced.things <= ITEM_CAP, f"{priced.things} things to order is over the {ITEM_CAP}-item cap"
    assert covers(menu, target, priced.units), "the order doesn't cover every wanted item (or has unrequested add-ons)"
    requested = Counter(a for w in wanted_units(target) for a in w.get("add_ons", []))
    in_order = Counter(a for u in priced.units for a in u["add_ons"])
    assert in_order == requested, "add-ons in the order must be exactly the ones asked for"
    if target["max_price_cents"] is not None:
        assert priced.total <= target["max_price_cents"], "the order is over budget"
    if uses_calories(target):
        assert priced.complete, "the target uses calories, so items with no calorie data must be left out"
    if target["min_calories"] is not None:
        assert priced.calories >= target["min_calories"], "the order is under the calorie minimum"
    if target["max_calories"] is not None:
        assert priced.calories <= target["max_calories"], "the order is over the calorie maximum"


# ------------------------------------------------------------------ the file

def test_case_ids_are_unique():
    ids = [c["id"] for c in CASES]
    assert len(ids) == len(set(ids))


def test_every_tricky_part_is_covered():
    used = {check for c in CASES for check in c["checks"]}
    assert used == set(DOC["checks"]), f"unused: {set(DOC['checks']) - used}, unknown: {used - set(DOC['checks'])}"


def test_every_result_kind_is_covered():
    assert {c["expected"]["result"] for c in CASES} == set(RESULTS)


# ------------------------------------------------------------------ each case

@pytest.mark.parametrize("case", CASES, ids=[c["id"] for c in CASES])
def test_target_is_valid_and_on_the_menu(case):
    target = case["target"]
    Target.from_dict(target)  # raises TargetFormatError if the shape is wrong
    assert target["chain"] in CHAINS, f"no chain file for '{target['chain']}'"
    chain = CHAINS[target["chain"]]
    menu = Menu(chain)
    for wanted in target["items"]:
        if wanted.get("item"):
            assert wanted["item"] in menu.items, f"'{wanted['item']}' isn't on the menu (stale case?)"
            if wanted.get("size"):
                menu.size(wanted["item"], wanted["size"])
        else:
            assert wanted["category"] in chain["categories"]
        for add_on in wanted.get("add_ons", []):
            assert add_on in menu.add_ons, f"add-on '{add_on}' isn't on the menu"
    assert case["checks"] and case["hand_calculation"]


@pytest.mark.parametrize("case", CASES, ids=[c["id"] for c in CASES])
def test_expected_answer_adds_up(case):
    target, expected = case["target"], case["expected"]
    assert expected["result"] in RESULTS
    if expected["result"] == "too_many_items":
        assert len(wanted_units(target)) > ITEM_CAP
        return
    assert len(wanted_units(target)) <= ITEM_CAP
    if expected["result"] == "no_valid_order":
        return

    menu = Menu(CHAINS[target["chain"]])
    priced = price_order(menu, expected["order"])
    assert priced.total == expected["total_cents"], (
        f"{case['id']}: the expected order costs {priced.total} cents at current prices, "
        f"but the case says {expected['total_cents']}. If prices changed, redo this hand calculation.")
    assert priced.calories == expected["calories"], f"calories add up to {priced.calories}"
    assert priced.complete == expected["calories_complete"]
    assert priced.separately == expected["separately_cents"], \
        f"bought one at a time it costs {priced.separately} cents"
    assert expected["separately_cents"] >= expected["total_cents"] or not any(
        "combo" in line or "bundle" in line for line in expected["order"]), \
        "a deal that costs more than its parts should never win"
    check_meets_target(menu, target, priced)

    for alternative in expected.get("equal_price_alternatives", []):
        other = price_order(menu, alternative)
        assert other.total == priced.total, f"alternative costs {other.total}, not {priced.total}"
        if target["goal"] == "most_calories":
            assert other.calories == priced.calories
        check_meets_target(menu, target, other)


def test_a_price_change_is_caught():
    """Raising a price must make the cases that use it stop adding up."""
    case = next(c for c in CASES if c["id"] == "mcd_big_mac_meal")
    changed = json.loads(json.dumps(CHAINS["mcdonalds"]))
    next(c for c in changed["combos"] if c["id"] == "big_mac_meal")["price_cents"] += 50
    priced = price_order(Menu(changed), case["expected"]["order"])
    assert priced.total == case["expected"]["total_cents"] + 50


def test_a_wrong_order_is_caught():
    """An order that's missing a wanted item must fail the target check."""
    case = next(c for c in CASES if c["id"] == "sample_combo_beats_parts")
    menu = Menu(CHAINS["sample_burger"])
    priced = price_order(menu, [{"item": "classic_burger", "size": "regular", "quantity": 1}])
    with pytest.raises(AssertionError, match="cover"):
        check_meets_target(menu, case["target"], priced)
