"""Task 2.1 tests: the shared target examples read and write the same way
in Python as in the C++ engine (which reads the same file)."""

import json
from pathlib import Path

import pytest

from combobite.target import Target, TargetFormatError, WantedItem

EXAMPLES = json.loads(
    (Path(__file__).resolve().parents[2] / "tests" / "target_examples.json").read_text(encoding="utf-8")
)


@pytest.mark.parametrize("example", EXAMPLES["valid"], ids=lambda e: e["name"])
def test_valid_examples_round_trip(example):
    target = Target.from_dict(example["target"])
    assert target.to_dict() == example.get("canonical", example["target"])
    assert Target.from_json(target.to_json()) == target


@pytest.mark.parametrize("example", EXAMPLES["invalid"], ids=lambda e: e["name"])
def test_invalid_examples_are_rejected(example):
    with pytest.raises(TargetFormatError):
        Target.from_dict(example["target"])


def test_error_names_the_field():
    bad = {"format_version": 1, "items": [{"item": "big_mac"}, {"item": "fries", "quantity": 0}]}
    with pytest.raises(TargetFormatError, match=r"items\[1\]\.quantity"):
        Target.from_dict(bad)


def test_bad_json_text():
    with pytest.raises(TargetFormatError, match="not valid JSON"):
        Target.from_json("{not json")


def test_build_in_code():
    target = Target(chain="wendys", max_price_cents=800, items=[WantedItem(category="burger", quantity=2)])
    data = target.to_dict()
    assert data["items"] == [{"category": "burger", "quantity": 2, "size": None, "add_ons": []}]
    assert data["goal"] == "cheapest"
