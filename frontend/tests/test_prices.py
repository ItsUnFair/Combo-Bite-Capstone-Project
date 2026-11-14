"""Tests for `combobite --refresh-prices`.

The menu rows come from tests/price_fixtures/, which are made-up rows in the
shape each price source documents. No test touches the internet."""

import datetime
import io
import json
import shutil
import urllib.error
from pathlib import Path

import pytest

from combobite import engine, prices
from combobite.cli import main

ROOT = Path(__file__).resolve().parents[2]
FIXTURES = ROOT / "tests" / "price_fixtures"
TODAY = datetime.date(2026, 11, 13)
SOURCES = prices.load_sources()


def rows(chain_id):
    return json.loads((FIXTURES / f"{chain_id}.json").read_text(encoding="utf-8"))


@pytest.fixture
def data(tmp_path):
    """A private copy of the real chain files."""
    folder = tmp_path / "chains"
    folder.mkdir()
    for path in (ROOT / "data" / "chains").glob("*.json"):
        shutil.copy(path, folder / path.name)
    return folder


def price(folder, chain_id, item_id, size="regular"):
    chain = json.loads((folder / f"{chain_id}.json").read_text(encoding="utf-8"))
    item = next(i for i in chain["items"] if i["id"] == item_id)
    return next(s for s in item["sizes"] if s["size"] == size)["price_cents"]


def by_key(result):
    return {change.entry.key: change for change in result.changes}


def passing_check(folder):
    return 0, ""


try:
    engine.find_engine()
    HAVE_ENGINE = True
except engine.EngineNotFound:
    HAVE_ENGINE = False
needs_engine = pytest.mark.skipif(not HAVE_ENGINE, reason="the C++ engine isn't built")


# ----------------------------------------------------------------- matching --

@pytest.mark.parametrize("a, b", [
    ("Whopper Jr.®", "whopper jr"),
    ("4 Pc. Chicken Nuggets", "4 piece chicken nuggets"),
    ("Chips & Nacho Cheese Sauce", "Chips and Nacho Cheese Sauce"),
    ("French Fries | Medium", "french fries|medium"),
])
def test_names_that_should_match(a, b):
    assert prices.normalize(a) == prices.normalize(b)


def test_offers_skip_free_unavailable_and_ambiguous_rows():
    offers, ambiguous = prices.build_offers(rows("burger_king"))
    assert offers["whopper"] == 619
    assert offers["french fries | medium"] == 419
    assert "free sauce" not in offers                 # $0 rows aren't prices
    assert "royal crispy chicken" not in offers       # marked unavailable
    assert "hash browns" in ambiguous and "hash browns" not in offers


def test_each_price_gets_the_right_status(data):
    result = prices.refresh_chain(data / "burger_king.json", SOURCES["burger_king"], rows("burger_king"),
                                  today=TODAY, dry_run=True)
    changes = by_key(result)
    assert (changes["whopper/regular"].status, changes["whopper/regular"].new_cents) == ("updated", 619)
    assert changes["double_whopper/regular"].status == "unchanged"
    assert changes["fries/medium"].new_cents == 419            # matched as a variant of "French Fries"
    assert changes["nuggets/4 pc"].new_cents == 249            # "Crown Nuggets" found as "Chicken Nuggets"
    assert changes["coca_cola/medium"].status == "unchanged"   # "Medium Coca-Cola®"
    assert changes["bacon_king/regular"].status == "held"      # $12.49 -> $1.99 is not believable
    assert changes["royal_crispy_chicken/regular"].status == "no match"
    assert changes["combo:whopper_meal"].status == "no match"  # needs an alias
    assert result.store.number == "99999" and result.store.city == "Testville"


def test_alias_matches_a_combo_to_a_variant(data):
    source = {**SOURCES["burger_king"],
              "aliases": {"combo:whopper_meal": ["Whopper | Medium - Side and Drink Included"]}}
    result = prices.refresh_chain(data / "burger_king.json", source, rows("burger_king"), today=TODAY, dry_run=True)
    assert by_key(result)["combo:whopper_meal"].new_cents == 1029


def test_allow_big_changes_applies_held_prices(data):
    result = prices.refresh_chain(data / "burger_king.json", SOURCES["burger_king"], rows("burger_king"),
                                  today=TODAY, dry_run=True, allow_big=True)
    assert by_key(result)["bacon_king/regular"].status == "updated"


# ------------------------------------------------------------------- saving --

def test_dry_run_changes_nothing(data):
    before = (data / "burger_king.json").read_bytes()
    result = prices.refresh_chain(data / "burger_king.json", SOURCES["burger_king"], rows("burger_king"),
                                  today=TODAY, dry_run=True)
    assert result.ok and not result.saved
    assert (data / "burger_king.json").read_bytes() == before


@needs_engine
def test_save_updates_prices_date_and_store(data):
    result = prices.refresh_chain(data / "burger_king.json", SOURCES["burger_king"], rows("burger_king"), today=TODAY)
    assert result.ok and result.saved
    assert price(data, "burger_king", "whopper") == 619
    assert price(data, "burger_king", "bacon_king") == 1249        # held price is untouched
    saved = json.loads((data / "burger_king.json").read_text(encoding="utf-8"))
    assert saved["prices_checked"]["date"] == "2026-11-13"
    assert "#99999" in saved["prices_checked"]["store"]
    assert "older estimates" in saved["prices_checked"]["store"]   # not every price matched
    assert engine.check_data_quiet(data)[0] == 0                   # the saved folder still passes the check


def test_failed_check_keeps_the_old_file(data):
    before = (data / "burger_king.json").read_bytes()
    result = prices.refresh_chain(data / "burger_king.json", SOURCES["burger_king"], rows("burger_king"),
                                  today=TODAY, check=lambda folder: (1, "  error    item 'x': bad"))
    assert not result.ok and not result.saved and "old prices were kept" in result.message
    assert (data / "burger_king.json").read_bytes() == before


@pytest.mark.parametrize("bad_rows", [[], [{"itemName": "Something Else", "price": 1.0}]])
def test_empty_or_unrelated_menu_changes_nothing(data, bad_rows):
    before = (data / "burger_king.json").read_bytes()
    result = prices.refresh_chain(data / "burger_king.json", SOURCES["burger_king"], bad_rows,
                                  today=TODAY, check=passing_check)
    assert not result.ok and "nothing was changed" in result.message
    assert (data / "burger_king.json").read_bytes() == before


# ------------------------------------------------------------ whole command --

def test_needs_a_place_and_a_token(data, monkeypatch, capsys):
    monkeypatch.delenv(prices.TOKEN_ENV_VAR, raising=False)
    assert prices.run_refresh(near=None, data_folder=data, dry_run=True) == 2
    assert "--near" in capsys.readouterr().out
    assert prices.run_refresh(near="60446", data_folder=data, dry_run=True) == 2
    assert prices.TOKEN_ENV_VAR in capsys.readouterr().out


def test_source_failure_keeps_prices_and_other_chains_continue(data, monkeypatch, capsys):
    monkeypatch.setenv(prices.TOKEN_ENV_VAR, "secret-token-value")
    before = (data / "burger_king.json").read_bytes()
    calls = []

    def fake_fetch(actor, run_input, token):
        calls.append((actor, run_input))
        if "burger-king" in actor:
            raise prices.PriceSourceError("couldn't reach Apify (network down).")
        return rows("taco_bell") if "taco-bell" in actor else []

    code = prices.run_refresh(near="60446", data_folder=data, dry_run=True, fetch=fake_fetch, today=TODAY)
    out = capsys.readouterr().out
    assert code == 1
    assert (data / "burger_king.json").read_bytes() == before
    assert "Burger King\n  NOT SAVED: couldn't reach Apify" in out and "were kept" in out
    assert "updated    Crunchy Taco: $1.99 -> $2.19" in out          # Taco Bell still went ahead
    assert "No price source is set up" in out                         # McDonald's
    assert "secret-token-value" not in out                            # the token is never printed
    assert all(run_input["addresses"] == ["60446"] for _, run_input in calls)


def test_from_raw_needs_no_token_or_internet(data, monkeypatch, capsys):
    monkeypatch.delenv(prices.TOKEN_ENV_VAR, raising=False)

    def no_fetch(*args):
        raise AssertionError("--from-raw must not use the internet")

    code = prices.run_refresh(near=None, data_folder=data, dry_run=True, from_raw=FIXTURES,
                              chains=["burger_king", "taco_bell"], fetch=no_fetch, today=TODAY)
    out = capsys.readouterr().out
    assert code == 0
    assert "updated    Whopper: $7.59 -> $6.19" in out
    assert "held       Bacon King" in out
    assert "Dry run: nothing was saved." in out
    assert "McDonald's" not in out                                    # --chain limits the run


def test_save_raw_writes_what_was_received(data, tmp_path, monkeypatch):
    monkeypatch.setenv(prices.TOKEN_ENV_VAR, "t")
    raw = tmp_path / "raw"
    prices.run_refresh(near="60446", data_folder=data, dry_run=True, save_raw=raw, chains=["taco_bell"],
                       fetch=lambda *a: rows("taco_bell"), today=TODAY)
    assert json.loads((raw / "taco_bell.json").read_text(encoding="utf-8")) == rows("taco_bell")


@needs_engine
def test_command_line_end_to_end(data, monkeypatch, capfd):
    monkeypatch.delenv(prices.TOKEN_ENV_VAR, raising=False)
    code = main(["--refresh-prices", "--from-raw", str(FIXTURES), "--data", str(data), "--chain", "taco_bell"])
    assert code == 0
    assert "Saved." in capfd.readouterr().out
    assert price(data, "taco_bell", "crunchy_taco") == 219
    assert price(data, "taco_bell", "fountain_drink", "medium") == 319     # "Medium Pepsi®" already matched


# ------------------------------------------------------------- the API call --

class FakeResponse(io.BytesIO):
    def __enter__(self):
        return self

    def __exit__(self, *exc):
        return False


def test_token_goes_in_the_header_not_the_url(monkeypatch):
    seen = {}

    def fake_urlopen(request, timeout):
        seen["url"] = request.full_url
        seen["auth"] = request.get_header("Authorization")
        seen["body"] = json.loads(request.data)
        return FakeResponse(b'[{"itemName": "Whopper", "price": 5.89}]')

    monkeypatch.setattr(prices.urllib.request, "urlopen", fake_urlopen)
    result = prices.fetch_rows("memo23~burger-king-menu-scraper", {"addresses": ["60446"]}, "tok123")
    assert result == [{"itemName": "Whopper", "price": 5.89}]
    assert seen["url"] == "https://api.apify.com/v2/actors/memo23~burger-king-menu-scraper/run-sync-get-dataset-items"
    assert "tok123" not in seen["url"] and seen["auth"] == "Bearer tok123"
    assert seen["body"] == {"addresses": ["60446"]}


@pytest.mark.parametrize("code, words", [(401, "rejected the API token"), (402, "out of credits"),
                                         (408, "longer than 5 minutes"), (500, "HTTP 500")])
def test_api_errors_become_plain_messages(monkeypatch, code, words):
    def fake_urlopen(request, timeout):
        raise urllib.error.HTTPError(request.full_url, code, "x", {}, io.BytesIO(b""))

    monkeypatch.setattr(prices.urllib.request, "urlopen", fake_urlopen)
    with pytest.raises(prices.PriceSourceError, match=words):
        prices.fetch_rows("a~b", {}, "tok123")


def test_no_connection_and_bad_replies(monkeypatch):
    def offline(request, timeout):
        raise urllib.error.URLError("no route to host")

    monkeypatch.setattr(prices.urllib.request, "urlopen", offline)
    with pytest.raises(prices.PriceSourceError, match="couldn't reach Apify"):
        prices.fetch_rows("a~b", {}, "t")
    monkeypatch.setattr(prices.urllib.request, "urlopen", lambda r, timeout: FakeResponse(b"<html>"))
    with pytest.raises(prices.PriceSourceError, match="valid JSON"):
        prices.fetch_rows("a~b", {}, "t")
    monkeypatch.setattr(prices.urllib.request, "urlopen", lambda r, timeout: FakeResponse(b'{"error": 1}'))
    with pytest.raises(prices.PriceSourceError, match="list of menu rows"):
        prices.fetch_rows("a~b", {}, "t")
