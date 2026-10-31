"""Task 9.1 check: every sample in tests/parser_samples.json is well formed,
and every expected target fits the target format and the chain data.

This doesn't call the LLM; it checks the expected answers themselves, so a
typo in the sample file shows up before the parser tests (task 9.2) run.
"""

import json
from pathlib import Path

import pytest

from combobite.target import Target

ROOT = Path(__file__).resolve().parents[2]
SAMPLES = json.loads((ROOT / "tests" / "parser_samples.json").read_text(encoding="utf-8"))["samples"]
CHAINS = {
    data["chain"]["id"]: data
    for data in (json.loads(p.read_text(encoding="utf-8")) for p in (ROOT / "data" / "chains").glob("*.json"))
}
ERROR_KINDS = {"not_food", "unknown_chain", "multiple_chains", "contradiction", "item_not_on_menu"}


def check_target_fits_menu(raw: dict) -> None:
    target = Target.from_dict(raw)
    if target.chain is None:
        assert not any(entry.item for entry in target.items), "exact items need a chain"
        return
    chain = CHAINS[target.chain]
    items = {item["id"]: item for item in chain["items"]}
    add_ons = {add_on["id"] for add_on in chain.get("add_ons", [])}
    for entry in target.items:
        if entry.item:
            assert entry.item in items, f"{entry.item} isn't on the {target.chain} menu"
            sizes = {s["size"] for s in items[entry.item]["sizes"]}
            assert entry.size is None or entry.size in sizes, f"{entry.item} has no size {entry.size}"
        else:
            assert entry.category in chain["categories"], f"{entry.category} isn't a {target.chain} category"
        assert set(entry.add_ons) <= add_ons
    if target.min_calories is not None and target.max_calories is not None:
        assert target.min_calories <= target.max_calories
    assert target.max_price_cents is None or target.max_price_cents >= 0


def test_ids_are_unique():
    ids = [sample["id"] for sample in SAMPLES]
    assert len(ids) == len(set(ids))


def test_covers_every_kind():
    kinds = {sample["kind"] for sample in SAMPLES}
    assert {"plain", "slang", "typo", "quantity", "calories", "flag", "error"} <= kinds


@pytest.mark.parametrize("sample", SAMPLES, ids=lambda s: s["id"])
def test_sample_is_well_formed(sample):
    assert sample["request"].strip()
    assert sample["session_chain"] is None or sample["session_chain"] in CHAINS
    expect = sample["expect"]
    assert ("target" in expect) != ("error" in expect)
    if "target" in expect:
        check_target_fits_menu(expect["target"])
        for alternative in sample.get("also_accept", []):
            check_target_fits_menu(alternative)
    else:
        assert expect["error"] in ERROR_KINDS
        for chain in expect.get("chains", []) + [expect.get("chain")]:
            assert chain is None or chain in CHAINS
        if "fallback_category" in expect:
            assert expect["fallback_category"] in CHAINS[expect["chain"]]["categories"]
