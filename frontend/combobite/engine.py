"""Finding and running the C++ engine program, combobite-engine.

For now this passes `combobite --check-data` through to the engine and lets
the price refresh check a file before saving it. The JSON bridge for
searching comes later (the engine bridge task).
"""

from __future__ import annotations

import os
import subprocess
import sys
from pathlib import Path

PROJECT_ROOT = Path(__file__).resolve().parents[2]
DEFAULT_DATA_FOLDER = PROJECT_ROOT / "data" / "chains"
ENGINE_ENV_VAR = "COMBOBITE_ENGINE"

_EXE = "combobite-engine.exe" if sys.platform == "win32" else "combobite-engine"
# Where CMake puts the program for each preset, including Visual Studio's
# Debug/Release subfolders.
_BUILD_FOLDERS = [
    Path("build", "default", "engine"),
    Path("build", "default", "engine", "Debug"),
    Path("build", "release", "engine"),
    Path("build", "release", "engine", "Release"),
]


class EngineNotFound(Exception):
    """The engine program isn't built, or COMBOBITE_ENGINE points nowhere."""


def engine_candidates(root: Path = PROJECT_ROOT) -> list[Path]:
    return [root / folder / _EXE for folder in _BUILD_FOLDERS]


def find_engine(root: Path = PROJECT_ROOT) -> Path:
    """Returns the engine program's path, or raises EngineNotFound saying where it looked."""
    override = os.environ.get(ENGINE_ENV_VAR)
    if override:
        path = Path(override)
        if path.is_file():
            return path
        raise EngineNotFound(f"{ENGINE_ENV_VAR} is set to {path}, but there's no file there.")

    candidates = engine_candidates(root)
    for path in candidates:
        if path.is_file():
            return path
    looked = "\n".join(f"  {path}" for path in candidates)
    raise EngineNotFound(
        "The ComboBite engine hasn't been built yet. Looked for it in:\n"
        f"{looked}\n"
        "Build it from the project folder with:\n"
        "  cmake --preset default\n"
        "  cmake --build --preset default"
    )


def run_check_data(data_folder: Path = DEFAULT_DATA_FOLDER, root: Path = PROJECT_ROOT) -> int:
    """Runs the engine's data checks, printing its report. Returns its exit code:
    0 = no errors, 1 = errors, 2 = nothing to check (or the engine is missing)."""
    try:
        engine = find_engine(root)
    except EngineNotFound as problem:
        print(problem, file=sys.stderr)
        return 2
    completed = subprocess.run([str(engine), "check-data", "--data", str(data_folder)], check=False)
    return completed.returncode


def check_data_quiet(data_folder: Path, root: Path = PROJECT_ROOT) -> tuple[int, str]:
    """Runs the engine's data checks on a folder and returns (exit code, report)
    without printing. Raises EngineNotFound if the engine isn't built."""
    completed = subprocess.run(
        [str(find_engine(root)), "check-data", "--data", str(data_folder)],
        check=False, capture_output=True, text=True,
    )
    return completed.returncode, completed.stdout
