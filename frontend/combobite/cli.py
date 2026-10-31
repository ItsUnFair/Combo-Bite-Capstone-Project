"""Entry point for the `combobite` command.

This is a placeholder from task 1.1 so the package installs and runs.
The real command loop, flags, and chain selection come in tasks 1.2 and 1.3.
"""

import argparse

from combobite import __version__


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        prog="combobite",
        description="Find the cheapest fast food order for what you want to eat.",
    )
    parser.add_argument("--version", action="version", version=f"combobite {__version__}")
    return parser


def main(argv: list[str] | None = None) -> int:
    build_parser().parse_args(argv)
    print(f"ComboBite {__version__}: setup is working. The ordering prompt isn't built yet.")
    return 0
