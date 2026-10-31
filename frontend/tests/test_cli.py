import pytest

from combobite import __version__
from combobite.cli import main


def test_main_runs(capsys):
    assert main([]) == 0
    assert "ComboBite" in capsys.readouterr().out


def test_version_flag(capsys):
    with pytest.raises(SystemExit) as exit_info:
        main(["--version"])
    assert exit_info.value.code == 0
    assert __version__ in capsys.readouterr().out
