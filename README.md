<p align="center">
  <img src="docs/logo.svg" width="160" alt="librarian bookshelf block">
</p>

<h1 align="center">librarian</h1>

<p align="center">
  <a href="https://github.com/Providence-Crafts/librarian/releases/latest"><img src="https://img.shields.io/github/v/release/Providence-Crafts/librarian" alt="latest release"></a>
</p>

A local retrieval-augmented question answering engine in pure C99: point it at
your documents, then ask questions and get answers grounded in, and cited
from, what you indexed — with no server, no Python and no network after setup.

librarian embeds documents with a small GGUF embedding model, stores the
vectors in SQLite through `sqlite-vec`, and answers with a local GGUF language
model through `llama.cpp`, all linked statically into one binary. It refuses
rather than guesses: no answer is generated unless retrieval clears a
similarity threshold and the model clears a confidence threshold, and every
answer lists the passages it drew on. Sibling of
[redstone](https://github.com/Providence-Crafts/redstone), a SQLite shell; same
design language. `librarian --help` is the full reference; this file is the
tour.

<p align="center"><img src="docs/demo.gif" alt="librarian demo: ingest a folder in chat, then ask a question and get a cited answer" width="720"></p>

## Install

Every [release](https://github.com/Providence-Crafts/librarian/releases)
carries a ready-to-run executable for each platform, plus an archive that adds
the README, the licence and an example `librarian.toml`. SQLite, sqlite-vec and
llama.cpp are compiled in.

**Linux** (x86-64, glibc 2.35 or later): download `librarian-X.Y.Z-linux-x86_64`,
then `chmod +x` it and put it on your `PATH` as `librarian`, or take
`librarian-X.Y.Z-linux-x86_64.tar.gz`. The C++ and OpenMP runtimes are linked
in, so it needs nothing beyond glibc.

**Windows** (10 1809 or later, x64): `winget install ProvidenceCrafts.librarian`,
or download `librarian-X.Y.Z-windows-x86_64.exe` (a single static executable)
and put it on your `PATH` as `librarian.exe`. `librarian-X.Y.Z-windows-x86_64.zip`
holds the same executable with the docs.

Check a download against `SHA256SUMS` in the same release. **From source**: see
[Build](#build); Nix is optional.

Models are not bundled. The first run of `librarian setup` downloads the two
default models (about 3.5 GB) from Hugging Face with `curl`.

## Build

A C99 compiler, a C++ compiler and CMake for llama.cpp, `make`, `git` and
`curl`. The Makefile uses clang, then gcc, then `cc`, whichever it finds first
(clang also needs libomp; otherwise `make CC=gcc`). `./setup.sh` fetches
llama.cpp and tomlc99 at pinned commits into `vendor/` and builds llama.cpp;
SQLite, sqlite-vec and miniz are already in the repository.

**Ubuntu / Debian** (22.04 or later):

```sh
sudo apt install build-essential cmake git curl unzip
./setup.sh             # fetch and build the pinned llama.cpp, fetch tomlc99
make release           # bin/librarian
make test              # unit tests under ASan+UBSan
```

**Windows**, in an [MSYS2](https://www.msys2.org) UCRT64 shell:

```sh
pacman -S --needed make git curl unzip mingw-w64-ucrt-x86_64-gcc mingw-w64-ucrt-x86_64-cmake mingw-w64-ucrt-x86_64-ninja
SKIP_LLAMA_BUILD=1 ./setup.sh   # sources only; make windows builds its own llama.cpp
make windows                    # bin/librarian.exe, static
```

**Nix**: the flake pins every tool used in development.

```sh
nix develop            # enter the dev shell once, then run the rest from inside it
./setup.sh
make                   # optimised build -> bin/librarian
make debug             # debug build
make test              # unit tests under ASan+UBSan
make gate              # format check, -Werror build, tests, cppcheck, clang-tidy -> PASS
```

`make help` lists every target, including `valgrind`, `tsan`, `tidy`,
`cppcheck`, `logo`, `compdb` and `watch`. `nix develop .#windows`
cross-compiles `librarian.exe` from Linux (`make windows`).

CI (`.github/workflows/ci.yml`) runs the gate in the Nix shell, the Ubuntu
route without Nix, and the MSYS2 build on Windows. Pushing a `v*` tag
publishes the executables and archives (`release.yml`).

## Usage

```sh
librarian setup                          # download the default models and show the quickstart
librarian ingest ~/notes                 # index a file or a directory, recursively
librarian query "How is the index built?"  # one question, answered and cited
librarian chat                           # interactive session with /commands and completion
```

`ingest` reads plain text, Markdown, Org, reStructuredText, AsciiDoc, LaTeX,
HTML, PDF, EPUB, ODT and DOCX in C (PDF text extraction is better with
`pdftotext` on `PATH`; legacy `.doc` needs `antiword`); unchanged files are
skipped by content hash on the next run. `docs [pattern]`
and `chunks <id>` inspect what is indexed, and `reset` clears it. Inside
`chat`, any line not starting with `/` is a question; `/help` lists the
commands (`/ingest`, `/docs`, `/chunks`, `/stats`, `/config`, `/reload`,
`/setup`, `/debug`, `/reset`, `/clear`, `/exit`).

Configuration is `librarian.toml`: database path, both model paths, chunk
size and overlap, and the similarity and confidence thresholds. It is read from
`$XDG_CONFIG_HOME/librarian/` (default `~/.config/librarian/`), and the
database, log and models live under `$XDG_DATA_HOME/librarian/` (default
`~/.local/share/librarian/`). When the working directory holds a
`librarian.toml` or a `data/` directory, librarian uses that instead, so a
checkout of this repository is a self-contained workspace.

## Keybindings

The `chat` line editor is emacs-flavoured; history is kept in
`~/.local/state/librarian/history`.

| Key | Action |
|---|---|
| `←` `→` | Move the cursor one character |
| `Ctrl-A` / `Ctrl-E`, `Home` / `End` | Jump to start / end of line |
| `Backspace` / `Delete` | Delete before / under the cursor |
| `↑` `↓` | Walk history |
| `Ctrl-L` | Clear the screen and repaint the banner |
| `Ctrl-C` | Abandon the current line |
| `Ctrl-D` | Exit on an empty line, delete-forward otherwise |
| `Tab` | Complete a `/command`, or a path after `/ingest`; opens a menu when several match |

**Completion menu**, once open

| Key | Action |
|---|---|
| `Tab` `→` / `Shift-Tab` `←` | Next / previous candidate |
| `↑` `↓` | Move the selection by row |
| `Enter` | Accept the selection |
| `Esc` / `Ctrl-C` | Dismiss |

## Output modes and theming

Output is human-readable text only; there are no alternative output modes.

Colours come from the shared theme engine (`theme.c`, identical in redstone).
The theme file is `$XDG_CONFIG_HOME/librarian/theme` (default
`~/.config/librarian/theme`; `%APPDATA%\librarian\theme` on Windows), an INI
file with `[brand]` (`glyph`, `name`, `tagline`, `facts`), `[status]`
(`success`, `progress`, `error`, `note`, `info`, `heading`) and `[menu]`
(`selected`, `match`, `detail`, `hint`) sections. Built-in themes are
`default` (truecolor), `dark`, `light` and `basic` (the eight ANSI colours).
`NO_COLOR` and `TERM=dumb` turn colour off; the bookshelf banner is drawn only
with colour, a UTF-8 locale, truecolor and at least 67 columns.

## Parity

Does not apply: librarian makes no compatibility claim against another tool.

## Platforms

Linux x86-64 is the primary platform; the gate runs there. Windows 10 1809+
gets a static `.exe` built with MinGW-w64, cross-compiled
(`nix develop .#windows`, then `make windows`, which builds its own
`llama.cpp`) or natively under MSYS2 in CI. It is exercised under Wine (ingest
and cited answers work) but not yet on real Windows, and the unit tests do not
run there; the line editor falls back to plain line input without completion or
history keys. macOS
builds from source with config and data under
`~/Library/Application Support/librarian/`. CPU inference only: `setup.sh`
builds `llama.cpp` without GPU back ends.

## Development

[`PROJECT.md`](PROJECT.md) holds the goals, the gate, the roadmap and the
checks still outstanding. [`docs/ARCHITECTURE.md`](docs/ARCHITECTURE.md)
describes the design, and
[`docs/development-workflow.md`](docs/development-workflow.md) the way work is
planned and done. [`docs/family.md`](docs/family.md) is the design contract
shared with redstone. `docs/pipeline_ingestion_architecture.md` and
`docs/cross_platform_distribution_and_self_contained_pipeline.md` go into more
depth on ingestion and packaging.

`make gate` is the single definition of done; `make logo` regenerates
`docs/logo.svg` from the banner bitmap in `src/brand_art.h`.

## License

GNU General Public License v3.0 or later (`GPL-3.0-or-later`); see [`LICENSE`](LICENSE).
