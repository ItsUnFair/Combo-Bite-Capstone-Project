"""Entry point for the `combobite` command.

Working so far:
  combobite --version
  combobite --check-data [--data <folder>]   (task 6.4, runs the C++ engine's checks)
  combobite --refresh-prices --near <ZIP or address>   (pulls one store's prices per chain)
The real command loop and chain selection come in tasks 1.2 and 1.3.
"""

import argparse
from pathlib import Path

from combobite import __version__
from combobite.engine import DEFAULT_DATA_FOLDER, run_check_data
from combobite.prices import run_refresh


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        prog="combobite",
        description="Find the cheapest fast food order for what you want to eat.",
    )
    parser.add_argument("--version", action="version", version=f"combobite {__version__}")
    parser.add_argument(
        "--check-data",
        action="store_true",
        help="check every chain data file and list errors and warnings by chain and entry",
    )
    parser.add_argument(
        "--data",
        type=Path,
        default=DEFAULT_DATA_FOLDER,
        metavar="FOLDER",
        help=f"folder holding the chain data files (default: {DEFAULT_DATA_FOLDER})",
    )
    refresh = parser.add_argument_group("refreshing prices (see docs/price-refresh.md)")
    refresh.add_argument(
        "--refresh-prices",
        action="store_true",
        help="pull current prices from one store per chain and save them after they pass the data check",
    )
    refresh.add_argument("--near", metavar="PLACE", help="ZIP code, city or street address to find the nearest store")
    refresh.add_argument("--chain", action="append", metavar="ID", help="only refresh this chain (can be repeated)")
    refresh.add_argument("--dry-run", action="store_true", help="show what would change without saving")
    refresh.add_argument("--allow-big-changes", action="store_true", help="apply prices that moved by more than 50%%")
    refresh.add_argument("--save-raw", type=Path, metavar="FOLDER", help="also save each store's menu as it was received")
    refresh.add_argument("--from-raw", type=Path, metavar="FOLDER", help="use menus saved by --save-raw instead of the internet")
    return parser


def main(argv: list[str] | None = None) -> int:
    args = build_parser().parse_args(argv)
    if args.check_data:
        return run_check_data(args.data)
    if args.refresh_prices:
        return run_refresh(
            near=args.near,
            data_folder=args.data,
            chains=args.chain,
            dry_run=args.dry_run,
            allow_big=args.allow_big_changes,
            save_raw=args.save_raw,
            from_raw=args.from_raw,
        )
    print(f"ComboBite {__version__}: setup is working. The ordering prompt isn't built yet.")
    return 0
