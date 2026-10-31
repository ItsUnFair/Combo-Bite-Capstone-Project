"""The target format (task 2.1): what the customer wants, in a fixed structure.

See docs/target-format.md. The C++ engine reads the same JSON with
engine/include/combobite/target.hpp.

Reading a target checks its shape only. Whether the chain, items and numbers
make sense for the menu is checked separately (task 2.4).
"""

from __future__ import annotations

import json
from dataclasses import dataclass, field
from typing import Any

FORMAT_VERSION = 1
GOALS = ("cheapest", "most_calories")


class TargetFormatError(ValueError):
    """The JSON doesn't have the shape of a target."""


def _optional_int(data: dict, key: str, where: str) -> int | None:
    value = data.get(key)
    if value is None:
        return None
    if isinstance(value, bool) or not isinstance(value, int):
        raise TargetFormatError(f"{where}{key} must be a whole number or null")
    return value


def _optional_str(data: dict, key: str, where: str) -> str | None:
    value = data.get(key)
    if value is None:
        return None
    if not isinstance(value, str) or not value:
        raise TargetFormatError(f"{where}{key} must be a non-empty string or null")
    return value


def _str_list(data: dict, key: str, where: str) -> list[str]:
    value = data.get(key)
    if value is None:
        return []
    if not isinstance(value, list) or not all(isinstance(v, str) and v for v in value):
        raise TargetFormatError(f"{where}{key} must be a list of non-empty strings")
    return list(value)


@dataclass
class WantedItem:
    """One thing the customer wants: an exact item or any item in a category."""

    item: str | None = None
    category: str | None = None
    quantity: int = 1
    size: str | None = None
    add_ons: list[str] = field(default_factory=list)

    @classmethod
    def from_dict(cls, data: Any, where: str = "") -> WantedItem:
        if not isinstance(data, dict):
            raise TargetFormatError(f"{where.rstrip('.')} must be an object")
        item = _optional_str(data, "item", where)
        category = _optional_str(data, "category", where)
        if (item is None) == (category is None):
            raise TargetFormatError(f"{where.rstrip('.')} needs exactly one of item or category")
        quantity = data.get("quantity", 1)
        if isinstance(quantity, bool) or not isinstance(quantity, int) or quantity < 1:
            raise TargetFormatError(f"{where}quantity must be a whole number of 1 or more")
        return cls(
            item=item,
            category=category,
            quantity=quantity,
            size=_optional_str(data, "size", where),
            add_ons=_str_list(data, "add_ons", where),
        )

    def to_dict(self) -> dict:
        out: dict[str, Any] = {"item": self.item} if self.item is not None else {"category": self.category}
        out.update(quantity=self.quantity, size=self.size, add_ons=list(self.add_ons))
        return out


@dataclass
class Target:
    """What the customer wants. See docs/target-format.md for each field."""

    chain: str | None = None
    goal: str = "cheapest"
    max_price_cents: int | None = None
    min_calories: int | None = None
    max_calories: int | None = None
    items: list[WantedItem] = field(default_factory=list)
    unsupported: list[str] = field(default_factory=list)
    format_version: int = FORMAT_VERSION

    @classmethod
    def from_dict(cls, data: Any) -> Target:
        if not isinstance(data, dict):
            raise TargetFormatError("a target must be a JSON object")
        version = data.get("format_version")
        if isinstance(version, bool) or not isinstance(version, int) or version != FORMAT_VERSION:
            raise TargetFormatError(f"format_version must be {FORMAT_VERSION}, got {version!r}")
        goal = data.get("goal")
        if goal is None:
            goal = "cheapest"
        if goal not in GOALS:
            raise TargetFormatError(f"goal must be one of {', '.join(GOALS)}, got {goal!r}")
        items = data.get("items")
        if items is None:
            items = []
        if not isinstance(items, list):
            raise TargetFormatError("items must be a list")
        return cls(
            chain=_optional_str(data, "chain", ""),
            goal=goal,
            max_price_cents=_optional_int(data, "max_price_cents", ""),
            min_calories=_optional_int(data, "min_calories", ""),
            max_calories=_optional_int(data, "max_calories", ""),
            items=[WantedItem.from_dict(entry, f"items[{i}].") for i, entry in enumerate(items)],
            unsupported=_str_list(data, "unsupported", ""),
        )

    def to_dict(self) -> dict:
        return {
            "format_version": self.format_version,
            "chain": self.chain,
            "goal": self.goal,
            "max_price_cents": self.max_price_cents,
            "min_calories": self.min_calories,
            "max_calories": self.max_calories,
            "items": [entry.to_dict() for entry in self.items],
            "unsupported": list(self.unsupported),
        }

    @classmethod
    def from_json(cls, text: str) -> Target:
        try:
            data = json.loads(text)
        except json.JSONDecodeError as error:
            raise TargetFormatError(f"not valid JSON: {error}") from error
        return cls.from_dict(data)

    def to_json(self, indent: int | None = None) -> str:
        return json.dumps(self.to_dict(), indent=indent)
