"""Task 6.4 tests for `combobite --check-data`: finding the engine, the
message when it isn't built, and the real data check when it is."""

import sys

import pytest

from combobite import engine
from combobite.cli import main


def test_engine_found_in_build_folder(tmp_path, monkeypatch):
    monkeypatch.delenv(engine.ENGINE_ENV_VAR, raising=False)
    fake = engine.engine_candidates(tmp_path)[1]  # a Visual Studio Debug folder
    fake.parent.mkdir(parents=True)
    fake.write_text("")
    assert engine.find_engine(tmp_path) == fake


def test_missing_engine_says_where_it_looked_and_how_to_build(tmp_path, monkeypatch):
    monkeypatch.delenv(engine.ENGINE_ENV_VAR, raising=False)
    with pytest.raises(engine.EngineNotFound) as problem:
        engine.find_engine(tmp_path)
    message = str(problem.value)
    assert str(tmp_path) in message
    assert "cmake --build --preset default" in message


def test_env_var_pointing_nowhere(tmp_path, monkeypatch):
    monkeypatch.setenv(engine.ENGINE_ENV_VAR, str(tmp_path / "missing-engine"))
    with pytest.raises(engine.EngineNotFound, match=engine.ENGINE_ENV_VAR):
        engine.find_engine(tmp_path)


def test_check_data_without_engine_exits_2(tmp_path, monkeypatch, capsys):
    monkeypatch.setenv(engine.ENGINE_ENV_VAR, str(tmp_path / "missing-engine"))
    assert main(["--check-data"]) == 2
    assert "missing-engine" in capsys.readouterr().err


def _built_engine():
    try:
        return engine.find_engine()
    except engine.EngineNotFound:
        return None


needs_engine = pytest.mark.skipif(_built_engine() is None, reason="the C++ engine isn't built")


@needs_engine
def test_check_data_on_real_chains_passes(monkeypatch, capfd):
    monkeypatch.delenv(engine.ENGINE_ENV_VAR, raising=False)
    assert main(["--check-data"]) == 0
    out = capfd.readouterr().out
    assert "4 chain files: 4 loaded" in out
    assert "0 errors" in out


@needs_engine
def test_check_data_reports_a_broken_file(tmp_path, monkeypatch, capfd):
    monkeypatch.delenv(engine.ENGINE_ENV_VAR, raising=False)
    (tmp_path / "broken.json").write_text('{ "format_version": 1,, }', encoding="utf-8")
    assert main(["--check-data", "--data", str(tmp_path)]) == 1
    out = capfd.readouterr().out
    assert "broken.json: NOT LOADED" in out
    assert "line 1" in out


@needs_engine
def test_check_data_missing_folder_exits_2(tmp_path, monkeypatch, capfd):
    monkeypatch.delenv(engine.ENGINE_ENV_VAR, raising=False)
    assert main(["--check-data", "--data", str(tmp_path / "nowhere")]) == 2
    assert "wasn't found" in capfd.readouterr().out


def test_engine_name_matches_the_platform():
    expected = "combobite-engine.exe" if sys.platform == "win32" else "combobite-engine"
    assert engine.engine_candidates()[0].name == expected
