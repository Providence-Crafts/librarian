---
title: "Librarian: Embedded Pure C99 RAG System"
id: "librarian"
#   Status rules:
#   * initiated/defined -> defined requirements exist
#   * in-research -> unknowns actively explored
#   * in-progress -> execution ongoing
#   * waiting condition -> blocked externally
#   * completed/finished -> goals met & verified
status: defined               # initiated | defined | in-research | in-progress | waiting | completed
priority: high                # critical | high | medium | low
start_date: "2026-10-01"      # ISO 8601: YYYY-MM-DD
target_date: ""               # ISO 8601: YYYY-MM-DD
last_updated: "2026-10-01"    # ISO 8601: YYYY-MM-DD
owner: "rs"                   # primary responsible person
stakeholders: []              # list of names or handles
tags: [c99, rag, llm, sqlite, sqlite-vec, llama-cpp, terminal-ui, embedded]
depends_on: []                # IDs of projects this one depends on
blocks: []                    # IDs of projects blocked by this one
references:
  - "prompt.md"
  - "notes.md"
  - "https://www.sqlite.org/download.html"
  - "https://github.com/asg017/sqlite-vec"
  - "https://github.com/ggml-org/llama.cpp"
  - "https://github.com/cktan/tomlc99"
notes: "Self-contained pure C99 RAG application with zero external runtime dependencies."
---

# Librarian: Embedded Pure C99 RAG System

## Overview

- **Purpose**: `librarian` is an ultra-fast, completely self-contained embedded Retrieval-Augmented Generation (RAG) system written in pure C99 with zero external runtime dependencies. It serves as a local, private knowledge companion capable of indexing local documents and answering questions grounded strictly in the provided text.
- **Context**: Existing RAG architectures typically rely on heavy Python runtimes, external daemon processes (vector databases, REST services), and complex multi-layered frameworks. `librarian` compiles into a single native binary integrating SQLite3, sqlite-vec, and llama.cpp's native C interface (`llama.h`) within a reproducible Nix development environment.
- **Scope**:
  - **In-Scope**:
    - Configuration file parsing (`librarian.toml`) via single-file C parser (`tomlc99`).
    - Pastel ANSI terminal interface with Minecraft bookshelf ASCII banner, confidence badges, and interactive REPL.
    - Transactional document chunking and vector storage via SQLite3 + `sqlite-vec` static amalgamation.
    - Text embedding extraction using local GGUF models via `llama.h` with L2 normalization.
    - Two-stage refusal logic to prevent hallucinations (Stage 1: retrieval distance threshold; Stage 2: generation token confidence and insufficient data marker).
    - Autoregressive text generation and token logprob evaluation via `llama.h`.
    - CLI modes: `ingest <path>`, `query "<prompt>"`, and interactive `chat` REPL with slash commands (`/ingest`, `/stats`, `/quit`).
  - **Out-of-Scope**:
    - Web / HTTP API server or GUI frontend.
    - Cloud vector database integrations or distributed network clustering.
    - Non-GGUF weight formats.
- **References**:
  - Technical requirements: [prompt.md](file:///home/rs/computation/programming/c/librarian/prompt.md)
  - Research notes & model links: [notes.md](file:///home/rs/computation/programming/c/librarian/notes.md)

## Goals

- **Goals**:
  - Build a completely self-contained native binary with zero runtime daemon or package dependencies.
  - Deliver sub-millisecond vector indexing and retrieval for personal document collections.
  - Implement a mathematically grounded two-stage refusal mechanism to prevent hallucination when knowledge is absent.
  - Provide an intuitive, modern, pastel-styled terminal interface with interactive REPL.
  - Maintain clean, robust, suckless C99 code verified with AddressSanitizer, UBSan, and Valgrind.
- **Success Criteria**:
  - Compiles cleanly with `-std=c99 -Wall -Wextra -pedantic` with zero warnings.
  - Passes unit tests and ASan/UBSan checks without memory leaks or undefined behavior.
  - Accurately chunks and embeds text documents in a single SQLite write transaction.
  - Refuses unanswerable queries at Stage 1 ($S = 1.0 - \text{distance} < \tau_{\text{retrieval}}$) without invoking generative inference.
  - Refuses low-confidence or unsupported answers at Stage 2 ($C_{\text{gen}} < \tau_{\text{gen}}$ or `[INSUFFICIENT_DATA]`).
  - Interactive REPL loads models once and handles `/ingest`, `/stats`, and `/quit` seamlessly.
- **Constraints / Priorities**:
  - Pure C99 conforming to suckless design principles (simple, composable, minimal abstraction).
  - Static amalgamations for SQLite3, sqlite-vec (`-DSQLITE_VEC_STATIC`), and tomlc99; static linkage to `libllama.a` and `libggml.a`.

## Development Guidelines

- **Environment**: Use Nix flakes (`nix develop`) to setup dependencies. Enter the nix shell once and perform all development inside it to prevent re-accessing every time.
- **Tooling & Commands**:
  - `make asan`: Build with AddressSanitizer and UndefinedBehaviorSanitizer (default dev build).
  - `make test`: Build and run unit test suite under AddressSanitizer.
  - `make release`: Build optimized release binary with `-O3 -flto`.
  - `make debug`: Build debug binary with symbols (`-g3 -O0`).
  - `make valgrind`: Run binary under Valgrind with full leak tracking.
  - `make tidy` / `make cppcheck`: Run static analysis linters.
  - `make format`: Auto-format code using `clang-format`.
  - `make compdb`: Generate `compile_commands.json` for `clangd` LSP via `bear`.
- **Version Control**: Use `git` to track changes. Commit every time a new phase or feature is implemented and verified to work as expected.
- **Workflow**: After each phase implementation is done, wait for explicit user confirmation before marking the verification boxes in the roadmap and committing.
- **Style**: Use suckless coding style and robust coding practices. Explicit memory ownership, minimal indirection, readable error returns.
- **Warnings**: Always address and fix compiler warnings immediately (`-Wall -Wextra -Wpedantic`).
- **Roadmap Expansion**: Once all currently defined phases are complete, expand the roadmap with consequent development phases and verification plans.

## Architecture

### Structure & File Layout

- `src/main.c`: CLI router (`ingest`, `query`, `chat`) and interactive REPL loop.
- `src/ui.c` / `include/ui.h`: ANSI pastel 24-bit color styling, Minecraft bookshelf ASCII art, status spinners (`[thinking...]`, `[searching...]`), and formatted confidence badges.
- `src/config.c` / `include/config.h`: Configuration loader parsing `librarian.toml` via `tomlc99`.
- `src/db.c` / `include/db.h`: SQLite3 and `sqlite-vec` initialization, schema migration, document/chunk persistence, and KNN vector search.
- `src/embedder.c` / `include/embedder.h`: Text chunking (sliding window with overlap), GGUF embedding model loader, and L2-normalized vector extraction using `llama.h`.
- `src/generator.c` / `include/generator.h`: Generative model inference, prompt templating, autoregressive token sampling, token logprob confidence calculation, and refusal detection.
- `vendor/`:
  - `vendor/sqlite/`: SQLite3 amalgamation (`sqlite3.c`, `sqlite3.h`).
  - `vendor/sqlite-vec/`: sqlite-vec amalgamation (`sqlite-vec.c`, `sqlite-vec.h`).
  - `vendor/tomlc99/`: Lightweight C99 TOML parser.
  - `vendor/llama.cpp/`: Local build producing `libllama.a` and `libggml.a`.

### Two-Stage Refusal Logic

1. **Stage 1 (Retrieval Refusal)**:
   - Calculate similarity score $S = 1.0 - \text{distance}$ from the KNN vector match in `sqlite-vec`.
   - If $S < \tau_{\text{retrieval}}$ (e.g. 0.65 configured in `librarian.toml`), the query is rejected immediately without invoking the generative LLM, conserving compute and eliminating hallucination risk.
2. **Stage 2 (Generation Refusal)**:
   - System prompt instructs the generative model to output `[INSUFFICIENT_DATA]` if the retrieved context is insufficient to answer the query.
   - Autoregressive generation tracks token logprobs and computes the mean sequence confidence:
     $$C_{\text{gen}} = \exp\left(\frac{1}{N} \sum_{i=1}^N \ln P(t_i)\right)$$
   - If the output contains `[INSUFFICIENT_DATA]` OR $C_{\text{gen}} < \tau_{\text{gen}}$ (e.g. 0.50), the system rejects the answer and emits an explicit refusal badge.

---

## Roadmap

**Hierarchy**: Phase → Task → Check

**Task status markers**

| Symbol | Meaning               |
|--------|-----------------------|
| `[ ]`  | To-do                 |
| `[~]`  | In-progress           |
| `[✓]`  | Done / completed      |
| `[x]`  | Failed / blocked      |
| `[?]`  | Optional / TBD        |
| `[!]`  | Critical              |

### General conditions

- Execution environment: Nix development shell (`nix develop`) with Clang toolchain, ASan, and UBSan.
- Coding standards: Pure C99, `-Wall -Wextra -Wpedantic`, zero compiler warnings, zero memory leaks.
- Git workflow: Logical commits per phase upon explicit user confirmation.

---

### Phase 1: Environment, Vendor Dependencies & Build System

**Description**
Set up vendor dependencies (`tomlc99`, `sqlite3`, `sqlite-vec`, `llama.cpp` static libraries), write `setup.sh` automation, and update `Makefile` to link amalgamations and static libraries into `bin/librarian`.

**Tasks**
- [✓] Create `setup.sh` to clone `tomlc99`, verify SQLite3 and sqlite-vec amalgamations, and compile `llama.cpp` static libraries (`libllama.a`, `libggml.a`).
- [✓] Adapt `Makefile` to compile `vendor/sqlite/sqlite3.c`, `vendor/sqlite-vec/sqlite-vec.c` (with `-DSQLITE_VEC_STATIC`), `vendor/tomlc99/toml.c`, and link against `libllama.a`, `libggml.a`, `libstdc++`, `m`, and `pthread`.
- [✓] Support targets `make asan`, `make release`, `make debug`, `make test`, `make compdb`.

**Checks**
- [✓] `setup.sh` completes cleanly and produces all vendor static artifacts.
- [✓] `make debug` and `make asan` successfully produce executable `bin/librarian` without errors or warnings.
- [✓] `make compdb` successfully generates `compile_commands.json` for `clangd`.

**Design decisions**
- Decision: Compile `sqlite3.c` and `sqlite-vec.c` as separate object files in `build/vendor/` rather than recompiling on every make run.
  Rationale: `sqlite3.c` is over 8 MB of C source; caching the object file speeds up incremental builds substantially.
  Trade-offs: Requires Makefile pattern rules specifically handling vendor directories.

**Dependencies**
- Nix development shell (`flake.nix`), vendor source trees.

---

### Phase 2: Configuration & Pastel UI Subsystem

**Description**
Implement the configuration module (`librarian.toml` loader) using `tomlc99`, and the pastel terminal UI module featuring ANSI escape styling, Minecraft bookshelf banner, progress indicators, and confidence badges.

**Tasks**
- [✓] Define configuration structures and API in `include/config.h` (`database`, `embedder`, `generator` settings).
- [✓] Implement TOML parser in `src/config.c` using `tomlc99`, handling defaults and missing keys.
- [✓] Implement `include/ui.h` and `src/ui.c` with pastel 24-bit ANSI colors (Lavender, Mint Green, Soft Peach, Powder Blue, Muted Gray, Reset).
- [✓] Add Minecraft bookshelf ASCII banner rendering and formatted confidence badges to `src/ui.c`.
- [✓] Write unit tests in `tests/test_config.c` and `tests/test_ui.c` using `minunit.h`.

**Checks**
- [✓] Configuration correctly parses sample `librarian.toml` and populates struct fields.
- [✓] UI banner renders correctly in terminal without color artifacts or line breaks.
- [✓] `make test` runs and passes with AddressSanitizer enabled.

---

### Phase 3: Storage Engine & Vector Database Subsystem

**Description**
Implement SQLite3 database initialization with `sqlite-vec` vector extension statically linked, schema migration, chunk storage, and KNN vector similarity queries.

**Tasks**
- [✓] Define DB interface in `include/db.h` (`db_open`, `db_close`, `db_insert_document`, `db_insert_chunk`, `db_search_knn`, `db_get_stats`).
- [✓] Implement `src/db.c` with `sqlite3_auto_extension((void (*)(void))sqlite3_vec_init)` under `-DSQLITE_VEC_STATIC`.
- [✓] Create database tables: `documents`, `chunks`, and virtual vector table `vec_chunks USING vec0(...)`.
- [✓] Implement single-transaction bulk chunk and vector insertion.
- [✓] Implement KNN vector search returning top-$K$ chunks with cosine/L2 distance and similarity score $S = 1.0 - \text{distance}$.
- [✓] Write unit tests in `tests/test_db.c` validating insertion and KNN search.

**Checks**
- [✓] Vector table creates successfully and accepts float vector embeddings.
- [✓] KNN search returns closest matches ranked by distance.
- [✓] Valgrind and ASan report zero memory leaks or uninitialized reads on DB lifecycle.

---

### Phase 4: Text Chunking & Embeddings Subsystem

**Description**
Implement the document text chunker with sliding window and overlap, and the embedding extraction pipeline using local GGUF models via `llama.h`.

**Tasks**
- [✓] Implement sliding window text chunker with configurable chunk size and token/word overlap in `src/embedder.c`.
- [✓] Implement GGUF embedding model loader and context management via `llama.h` in `src/embedder.c` and `include/embedder.h`.
- [✓] Extract embedding vectors from `llama_get_embeddings_seq` / `llama_get_embeddings_ith` and perform L2 normalization.
- [✓] Directory and file traversal to read text documents for ingestion.
- [✓] Write unit tests in `tests/test_embedder.c` testing chunking logic and embedding output dimensions.

**Checks**
- [✓] Chunker produces deterministic chunks with expected overlaps.
- [✓] GGUF embedding model loads and computes 1024-dimensional normalized vectors matching `harrier-oss-v1-0.6b.Q8_0.gguf`.
- [✓] L2 norm of generated vector equals $1.0 \pm 1e-4$.

---

### Phase 5: Generative Inference & Two-Stage Refusal Engine

**Description**
Implement generative inference using `llama.h`, prompt assembly with retrieved context, token logprob calculation, and the two-stage hallucination refusal logic.

**Tasks**
- [ ] Implement generative model loader and context initialization in `src/generator.c` and `include/generator.h` (`MiniCPM5-2B-Q8_0.gguf`).
- [ ] Format prompt template with retrieved context and system instruction to return `[INSUFFICIENT_DATA]` when uncertain.
- [ ] Implement autoregressive generation loop with temperature, top-k/top-p sampling, and token logprob accumulation.
- [ ] Compute mean sequence confidence $C_{\text{gen}} = \exp(\frac{1}{N} \sum_{i=1}^N \ln P(t_i))$.
- [ ] Implement Stage 1 refusal: reject if top retrieval match $S < \tau_{\text{retrieval}}$ before running generator.
- [ ] Implement Stage 2 refusal: reject if model outputs `[INSUFFICIENT_DATA]` OR $C_{\text{gen}} < \tau_{\text{gen}}$.
- [ ] Write unit tests in `tests/test_generator.c`.

**Checks**
- [ ] Model successfully generates answers grounded in provided context.
- [ ] Queries without matching documents in DB are refused immediately at Stage 1.
- [ ] Hallucinated or ambiguous queries trigger Stage 2 refusal badge.

---

### Phase 6: CLI Modes, Interactive REPL & System Verification

**Description**
Wire all subsystems into `src/main.c`, providing `ingest`, `query`, and `chat` REPL modes with slash commands, complete documentation, and full validation suite.

**Tasks**
- [ ] Implement CLI router in `src/main.c` supporting `ingest <path>`, `query "<prompt>"`, and `chat`.
- [ ] Implement interactive REPL loop maintaining models loaded in memory, with status indicators (`[thinking...]`, `[searching...]`).
- [ ] Implement REPL slash commands: `/ingest <path>`, `/stats`, `/quit`, `/exit`.
- [ ] Run full test suite with AddressSanitizer and LeakSanitizer (`make test`).
- [ ] Run Valgrind memory leak verification (`make valgrind`).
- [ ] Run static analysis audits (`make tidy`, `make cppcheck`).

**Checks**
- [ ] `librarian ingest` ingests sample files into SQLite DB.
- [ ] `librarian query` answers questions with pastel UI and confidence badges.
- [ ] `librarian chat` REPL responds interactively to queries and slash commands without crashing or memory leaks.
- [ ] `make test`, `make tidy`, and `make valgrind` pass with 0 errors and 0 warnings.
