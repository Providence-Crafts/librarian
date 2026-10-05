---
title: "librarian — local question answering over your own documents"
id: "librarian"
status: in-progress           # initiated | defined | in-research | in-progress | waiting | completed
priority: high                # critical | high | medium | low
start_date: "2026-10-01"
target_date: ""
last_updated: "2026-10-05"
owner: "rs"
stakeholders: []
tags: [c99, rag, llm, sqlite, sqlite-vec, llama-cpp, cli, suckless]
depends_on: []
blocks: []
references:
  - "docs/ARCHITECTURE.md"
  - "docs/development-workflow.md"
  - "docs/family.md"
  - "README.md"
  - "https://github.com/asg017/sqlite-vec"
  - "https://github.com/ggml-org/llama.cpp"
  - "https://github.com/cktan/tomlc99"
notes: "Phases 1-18 complete and gate-green; v0.1.0 and v0.1.1 released, v0.1.2 in progress. Manual checks outstanding below."
---

# librarian

## Overview

**Purpose.** Retrieval-augmented question answering usually means a Python
runtime, a vector-database daemon and a model server. `librarian` is the same
idea as one native binary: point it at your documents, ask questions, and get
answers grounded in, and cited from, what you indexed. It refuses rather than
guesses when the documents do not support an answer.

**Context.** C99 under suckless constraints. SQLite and `sqlite-vec` for
storage and vector search, `llama.cpp` for embedding and generation, tomlc99
for configuration, miniz for zip-based document formats, all linked statically.
Development happens in a pinned Nix flake; building from source does not need
it. Sibling of redstone, with which it shares a design language, a platform
layer and a theme engine (`docs/family.md`).

**Scope.** Ingesting files and directories in common text and office formats,
retrieval with a similarity threshold, generation with a confidence threshold,
references for every answer, a one-shot `query` mode and an interactive `chat`
REPL with completion. Linux and Windows 10 1809+.

**Explicitly excluded**, by decision: an HTTP API or GUI, remote vector
stores, non-GGUF model formats, bundling models in releases.

## Goals

**Goals**

1. **Grounded answers.** Every answer cites the passages it drew on.
2. **Honest refusal.** No answer when retrieval is weak or the model is unsure.
3. **One binary.** No daemon, no interpreter, no network after `librarian setup`.
4. **Useful at the terminal.** A chat REPL with history, completion and the
   family's themed output.
5. A codebase one person can read in a day.

**Success criteria**

- Ingest, a cited answer and a refusal work end to end on Linux, and under
  Wine for the Windows build.
- `make gate` prints PASS: formatter, `-Werror` build, sanitised tests,
  cppcheck and clang-tidy clean.
- The documented builds work from a clean checkout: Nix, Ubuntu 22.04+ with
  apt, and MSYS2 UCRT64. CI exercises all three.
- `src/` stays in the region of 9,000 lines. **Currently 9,280** including
  headers.

**Constraints**

- C99 (`-std=c99`) with `_GNU_SOURCE` for POSIX threads and file APIs; every
  platform difference behind `include/plat.h`.
- Library dependencies are vendored or fetched at pinned commits, never taken
  from the system: SQLite, sqlite-vec and miniz in `vendor/`; llama.cpp and
  tomlc99 by `setup.sh`.
- Optional runtime helpers only: `pdftotext` improves PDF extraction,
  `antiword` enables `.doc`, `curl` downloads models.

## Development Guidelines

- **Environment**: Nix flakes. `nix develop` provides the toolchain; enter the
  shell once and work inside it. `nix develop .#windows` cross-compiles.
- **Version control**: `git`. Atomic commits in conventional form
  (`feat(x):`, `fix:`, `build:`, `docs:`, `ci:`, `release:`); stage explicit
  paths, never stage-all. Tags `vX.Y.Z` publish releases.
- **Workflow**: `docs/development-workflow.md`. Planning is supervised,
  implementation is autonomous; undefined behaviour at implementation time is
  flagged, never guessed.
- **Manual checks do not block a commit.** Their boxes stay unticked until
  the owner has looked; outstanding ones are listed at the end of this file.
- **Style**: suckless. Explicit ownership, readable error returns, comments
  that explain why.
- **Warnings are defects.** Fixed at the cause or waived below with a reason.

### The gate

One command, one verdict: `make gate` → PASS or a non-zero exit.

| Step | Command | Fails on |
|------|---------|----------|
| format | `make format-check` | any file clang-format would change |
| build | `make clean release WARNINGS_AS_ERRORS=1` | any warning |
| tests | `make test WARNINGS_AS_ERRORS=1` (ASan + UBSan) | a failed test, a leak, UB |
| cppcheck | `make cppcheck` | any finding |
| clang-tidy | `make tidy` | any finding (`WarningsAsErrors: '*'`) |

CI runs the gate in the Nix shell, the apt route on Ubuntu (`make release`,
`make test`), and the MSYS2 build with a smoke run on Windows.

### Waived diagnostics

| Diagnostic | Where | Rationale |
|---|---|---|
| `readability-identifier-length` | `.clang-tidy` | Short loop and parameter names are clearer in dense code. |
| `readability-magic-numbers` | `.clang-tidy` | Buffer sizes and caps read better inline. |
| `readability-uppercase-literal-suffix` | `.clang-tidy` | Lowercase `u` is the prevailing idiom and has no `l`/`1` ambiguity. |
| `misc-include-cleaner` | `.clang-tidy` | Fights the convention that a header owns the includes its declarations need. |
| `bugprone-easily-swappable-parameters` | `.clang-tidy` | `(FILE *out, FILE *err)` and similar are deliberate. |
| `cert-err33-c` | `.clang-tidy` | A failed write to the terminal has no useful recovery. |
| `readability-function-cognitive-complexity` | `.clang-tidy` | Flat dispatchers (CLI router, REPL key loop, format extractors) score high without any deep branch. |
| `bugprone-multi-level-implicit-pointer-conversion` | `.clang-tidy` | Fires on `malloc` into `char **`; casting would hide a missing `<stdlib.h>`. |
| `missingIncludeSystem`, `checkersReport`, `unusedFunction`, `vendor/*` | `Makefile` cppcheck | About cppcheck's own analysis or third-party code, not `src/`. |
| `cert-env33-c` on `popen` | `src/doc.c`, inline | `pdftotext`/`antiword` are run on shell-escaped paths. |
| Mesa Vulkan driver leaks | `lsan.supp` | Leaks inside `libvulkan_radeon.so`, outside our code. |
| gcc 11 `-Wnull-dereference` in `art_row` | not waived | False positive on Ubuntu 22.04 only; both pointers are checked. Not an error outside the gate, which uses clang. |

## Architecture

Detailed design lives in `docs/ARCHITECTURE.md`; this is the shape.

```
main.c   CLI router, setup and model download, chat commands
  |
  +-- repl.c       line editor, history, /command and path completion
  +-- pipeline.c   ingestion: worker threads -> bounded queue -> consumer
  |     +-- doc.c        text extraction per format, binary/LFS detection
  |     +-- embedder.c   chunking, batched embedding (llama.h)
  +-- generator.c  prompt, sampling, confidence, refusal (llama.h)
  +-- db.c         SQLite + sqlite-vec: schema, documents, chunks, KNN
  +-- config.c     librarian.toml, workspace and per-platform paths
  +-- ui.c, brand.c, theme.c   output, banner, shared theme engine
  +-- plat_*.c     shared platform layer
  +-- logger.c     rotating log
```

**Key property.** Workers never touch SQLite or a model; the consumer owns
both. The only shared state is the queue.

**Resources.** llama.cpp `b11301-9`
(`f872b591121761ac7b2af18283bd99bdc092a63a`) and tomlc99
(`29076dfd095bbbbd50a3c1b2760d29f4b83e74ac`), pinned in `setup.sh`; SQLite
3.53.4 and sqlite-vec vendored. Default models: harrier-oss-v1 0.6B (Q8_0,
1024 dimensions) for embedding and MiniCPM5 2B (Q8_0) for generation, both
from Hugging Face.

## Roadmap

**Hierarchy**: Phase → Task → Check

| Symbol | Meaning          |
| ------ | ---------------- |
| `[ ]`  | To-do            |
| `[~]`  | In-progress      |
| `[✓]`  | Done / completed |
| `[x]`  | Failed / blocked |
| `[?]`  | Optional / TBD   |
| `[!]`  | Critical         |

### General conditions

Every phase ends with `make gate` green. Phases 1–16 predate this file and
were tracked in a local planning document; they are summarised here with
their outcome.

### Phases 1–16: the engine `[✓]`

- [✓] 1. Environment, vendored dependencies, `setup.sh` and the Makefile
- [✓] 2. Configuration (`librarian.toml`) and the terminal UI
- [✓] 3. Storage: SQLite with `sqlite-vec`, schema, KNN search
- [✓] 4. Sliding-window chunking and embeddings
- [✓] 5. Generation and the two-stage refusal
- [✓] 6. CLI modes (`ingest`, `query`, `chat`) and end-to-end verification
- [✓] 7. Interactive REPL and logging
- [✓] 8. Bookshelf banner, rotating logger, zsh-style completion menu
- [✓] 9. Batched multi-sequence embedding, CPU scaling, content-hash skip
- [✓] 10. PDF and EPUB extraction, binary filtering, UTF-8 sanitising
- [✓] 11. Optional Vulkan acceleration, configurable chunk geometry
- [✓] 12. `reset`, `/clear`, `/reload`, debug mode
- [✓] 13. Pipelined concurrent ingestion
- [✓] 14. References, ChatML prompts, vector health recovery
- [✓] 15. `docs` filter, compact document and chunk explorer
- [✓] 16. Pure-C extractors (HTML, DOCX, ODT, EPUB), Git LFS detection,
      pagination, per-platform paths, `librarian setup`, Windows build

### Phase 17: Family alignment and publication `[✓]`

**Tasks**

- [✓] Shared plat layer and theme engine re-synced from redstone; local
      workarounds removed; `plat_mkdir_p` and `plat_nprocs` adopted
- [✓] Line-editor buffer overflow on long lines fixed, with a pty test
- [✓] Per-user config read; defaults when none exists; schema created for
      every command on a fresh database
- [✓] Windows: static exe, C99 stdio, model sizes over 2 GiB
- [✓] llama.cpp and tomlc99 pinned in `setup.sh`
- [✓] CI, tag-driven releases, winget manifest, README per `docs/family.md`,
      VHS demo

**Checks**

*Automatic*

- [✓] `make gate` → PASS (23 tests), also with `TERM` unset
- [✓] CI green on both jobs (run 37241341966)
- [✓] v0.1.0 released; `sha256sum -c SHA256SUMS` OK

*Manual*

- [ ] Owner sign-off on the README and the demo

### Phase 18: Licence, documentation and builds without Nix `[✓]`

**Tasks**

- [✓] `LICENSE` (GPL-3.0-or-later) and the README section
- [✓] `PROJECT.md`, `docs/ARCHITECTURE.md`, `docs/development-workflow.md`
- [✓] Makefile picks clang, then gcc, then `cc`; MSYS2 uses native gcc
- [✓] README build instructions for Ubuntu/Debian (apt) and MSYS2 UCRT64
- [✓] CI job `ubuntu` without Nix
- [✓] Releases attach the bare executables; LICENSE in both archives
- [✓] Version 0.1.1 and `packaging/winget/0.1.1/`
- [✓] Release Linux executable with a static GCC runtime (`STATIC_RUNTIME=1`),
      guarded by `ldd` in the release job; version 0.1.2

**Checks**

*Automatic*

- [✓] Clean copy in `ubuntu:24.04` and `ubuntu:22.04`: `setup.sh`,
      `make release`, `make test` (23 tests)
- [✓] CI green on all three jobs (run 37273785819); v0.1.1 released,
      `sha256sum -c SHA256SUMS` OK, the exe runs under Wine
- [✓] The 0.1.2 Linux executable links only glibc and runs `--version`,
      `ingest` and `query` in bare `ubuntu:22.04`, `ubuntu:24.04` and
      `debian:stable-slim` containers (0.1.1 failed: no `libgomp.so.1`)

## Deferred work

| Item | Raised | Status |
|---|---|---|
| Unit tests on native Windows | Phase 17 | Open. Tests hard-code `/tmp` and use `posix_openpt`; they need `plat_temp_file` and a pty-free path before the Windows CI job can run them. |
| Completion and history keys on Windows | Phase 16 | Open. The REPL falls back to plain line input; needs a console-input path in `plat_win32.c`. |
| GPU back ends in release builds | Phase 11 | Open. Vulkan works from source; releases stay CPU-only to avoid a loader dependency. |
| Fully static Linux binary | Phase 17 | Partly done. Since 0.1.2 the release links libstdc++, libgomp and libgcc statically (`STATIC_RUNTIME=1`); glibc stays dynamic, so the floor is glibc 2.35. |
| macOS in CI | Phase 16 | Open. Builds from source; not exercised. |
| PDF extraction without `pdftotext` | Phase 16 | Open. The built-in FlateDecode fallback handles simple PDFs only. |

## Manual checks outstanding

| Phase | Check | How to reproduce |
|---|---|---|
| 17 | Answers are useful and refusals are right on a real collection | `librarian setup`, `librarian ingest ~/notes`, then ask questions you know the answer to and some you know are not covered. |
| 17 | librarian works interactively on real Windows 10/11 | Install from the release zip or `winget install --manifest packaging/winget/X.Y.Z`; run `librarian setup`, `ingest` and `chat` in Windows Terminal, `cmd.exe` and PowerShell. Check the banner, colours, Ctrl-C and paths under `%APPDATA%` and `%LOCALAPPDATA%`. |
| 17 | Owner sign-off on `README.md` and `docs/demo.gif` | Read and watch both; check they match how the program behaves. |
