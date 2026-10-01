You are an expert systems programmer specializing in C99, terminal UI design, SQLite3 C API, sqlite-vec, and llama.cpp's native C interface (`llama.h`).

Build `librarian`, a fast, self-contained embedded RAG (Retrieval-Augmented Generation) application in pure C99 with zero external runtime dependencies.

---

### 1. Aesthetic & UI Requirements
* **Color Palette:** Modern, playful pastel ANSI escape codes (24-bit RGB or 256-color):
  - Lavender: `\x1b[38;2;189;147;249m`
  - Mint Green: `\x1b[38;2;139;233;180m`
  - Soft Peach: `\x1b[38;2;255;184;108m`
  - Powder Blue: `\x1b[38;2;139;190;255m`
  - Muted Gray: `\x1b[38;2;98;114;164m`
  - Reset: `\x1b[0m`
* **Minecraft Bookshelf ASCII Art:** Display a clean, compact bookshelf block banner on launch and in the REPL:
  ```text
    +-----------+
   /|   === === |
  +-+-----------+
  | | [=] [=] [=]|  L I B R A R I A N
  | | [=] [=] [=]|  Your local, self-contained knowledge companion
  |/  === ===   |
  +-------------+

```

* **REPL Design:** Minimalist prompt (`librarian ❯ `), smooth status indicators (`[thinking...]`, `[searching...]`), and formatted confidence badges.

---

### 2. Configuration (`librarian.toml`)

Integrate a lightweight single-file C TOML parser (e.g., `tomlc99`) to dynamically load models and thresholds so users can swap weights without recompiling:

```toml
[database]
path = "data/librarian.db"

[embedder]
model_path = "models/harrier-oss-v1-0.6b.Q8_0.gguf"
dimension = 1024
similarity_threshold = 0.65

[generator]
model_path = "models/minicpm-2b-instruct-q4_k_m.gguf"
context_length = 2048
confidence_threshold = 0.50

```
---

### 3. CLI & Execution Modes

* **`librarian ingest <file_or_dir>`**
* Reads text files, chunks them (sliding window with overlap), computes embeddings, and persists them into SQLite using a single write transaction.

* **`librarian query "<question>"`**
* Runs one-off retrieval + generation pipeline and prints the result with confidence badges.

* **`librarian chat` (Interactive REPL)**
* Loads models into memory **once**.
* Interactive loop supporting standard queries and slash commands:
* `/ingest <path>`: Ingest new documents live.
* `/stats`: Display document count and vector storage usage.
* `/exit` or `/quit`: Clean shutdown.

---

### 4. Technical Architecture (Pure C99)

* **Code Layout:**
* `src/main.c`: CLI router & REPL loop.
* `src/ui.c` / `ui.h`: ASCII art banner, pastel print helpers, badges.
* `src/config.c` / `config.h`: TOML configuration parser.
* `src/db.c` / `db.h`: SQLite3 + `sqlite-vec` initialization, insertions, and KNN vector search.
* `src/embedder.c` / `embedder.h`: GGUF loading and L2-normalized vector extraction via `llama.h`.
* `src/generator.c` / `generator.h`: Autoregressive generation and sequence token logprob calculation via `llama.h`.

* **Two-Stage Refusal Logic:**
1. *Stage 1 (Retrieval):* Compute $S = 1.0 - \text{distance}$. If $S < \tau_{\text{retrieval}}$, refuse immediately without invoking the generative LLM.
2. *Stage 2 (Generation):* If model outputs `[INSUFFICIENT_DATA]` OR mean token logprob confidence $C_{\text{gen}} < \tau_{\text{gen}}$, return refusal.

* **Static Amalgamations & Linkage:**
* Compile `vendor/sqlite3.c` and `vendor/sqlite-vec.c` statically with `-DSQLITE_VEC_STATIC`.
* Link against `libllama.a` and `libggml.a`.

---

### 5. Setup & Build Files

Provide:

1. `setup.sh`: Automated script to fetch SQLite3 amalgamation, `sqlite-vec` release, clone `tomlc99`, and compile `llama.cpp` static libraries.
2. `Makefile`: Builds `bin/librarian` with `clang -std=c99` or `gcc -std=c99` with `-O3 -Wall -Wextra -pedantic`.

