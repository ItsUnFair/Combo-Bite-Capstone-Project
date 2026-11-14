# ComboBite

ComboBite finds the cheapest fast food order for what you want to eat. You
describe what you're hungry for in plain English, and it searches the
chain's combos, bundles and single items for the cheapest order that fits,
then explains why.

> Status: early development. The project builds and runs, but the ordering
> prompt and optimizer aren't written yet.

## How it's built

| Part | Language | Folder | What it does |
|---|---|---|---|
| Engine | C++17 | `engine/` | Loads and checks menu data, finds the cheapest order, runs optimizer tests |
| Front end | Python 3.10+ | `frontend/` | The `combobite` command, LLM request parsing, output and explanations |
| Menu data | JSON | `data/` | One file per chain (`data/chains/`), plus a format example in `data/sample/` |
| Test data | JSON | `tests/` | Target examples and parser sample requests shared by both halves |
| Docs | Markdown | `docs/` | [Data file format](docs/data-format.md), [target format](docs/target-format.md), [price refresh](docs/price-refresh.md) |

Chains covered: McDonald's, Wendy's, Burger King and Taco Bell. Their prices
are national-average estimates until they're checked at one store; see
[`data/chains/SOURCES.md`](data/chains/SOURCES.md).

## Requirements

- CMake 3.20 or newer
- A C++17 compiler: Visual Studio 2022 (MSVC), or GCC 9+ / Clang 10+
- Python 3.10 or newer
- An internet connection the first time you build, to download
  [nlohmann/json](https://github.com/nlohmann/json)

## Build the engine

From the project folder:

```
cmake --preset default
cmake --build --preset default
ctest --preset default
```

The engine program ends up in `build/default/engine/`
(`combobite-engine.exe` on Windows, inside a `Debug` folder with Visual Studio).
Check that it works:

```
combobite-engine --version
combobite-engine summary data/sample/sample_chain.json
```

**Visual Studio 2022:** choose *File → Open → Folder* and pick the project
folder. Visual Studio reads `CMakePresets.json` on its own; pick the
`default` preset and build. Install the "Desktop development with C++"
workload if you haven't.

**VS Code:** install the C/C++, CMake Tools and Python extensions, open the
folder, and pick the `default` preset when CMake Tools asks. On Windows you
also need the Build Tools for Visual Studio for the compiler.

## Install the front end

Create a virtual environment and install the package with its test tools:

```
python -m venv .venv
.venv\Scripts\activate          (Windows)
source .venv/bin/activate       (macOS / Linux)
pip install -e ".[dev]"
```

Then run it and its tests:

```
combobite --version
pytest
```

## Check the menu data

After building the engine, check every chain file:

```
combobite --check-data
```

It lists errors (the chain won't load) and warnings by chain and entry. See
[Checking your changes](docs/data-format.md#checking-your-changes) for every
rule. The front end finds the engine in `build/`; set the `COMBOBITE_ENGINE`
environment variable to use one somewhere else.

## Refresh prices from a store

With the engine built and a free Apify API token in the `APIFY_TOKEN`
environment variable, pull one store's prices per chain:

```
combobite --refresh-prices --near 60446 --dry-run
```

Drop `--dry-run` to save. Prices are only saved after they pass the data
check, and a failed pull keeps the last good prices. See
[docs/price-refresh.md](docs/price-refresh.md) for setup, cost and limits.

## API key

The front end will read the LLM API key from an environment variable (set
up in task 2.2). Never put the key in a file inside this folder; `.env` and
key files are already in `.gitignore`.
