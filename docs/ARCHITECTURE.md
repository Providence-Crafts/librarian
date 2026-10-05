# librarian Architecture

`librarian` answers questions about a local document collection. It embeds
documents with a GGUF embedding model, stores the vectors in SQLite through
`sqlite-vec`, and answers with a GGUF language model through `llama.cpp`,
citing the passages it used and refusing when retrieval or the model is not
confident enough.

This document records how the program is decomposed and why. Project state
(goals, roadmap, the gate) lives in `PROJECT.md`; the contract shared with
redstone lives in `docs/family.md`.

## Scope and non-goals

In scope: ingesting files and directories, retrieval, grounded generation with
references, a one-shot `query` mode and an interactive `chat` REPL, all in one
statically linked binary that needs no network after `librarian setup`.

Non-goals, stated so they are not accidentally pursued: an HTTP API or GUI,
remote vector stores, non-GGUF weight formats, and GPU back ends in the release
builds (the code supports Vulkan when `llama.cpp` is built with it, but
`setup.sh` builds CPU only).

## Data flow

```
ingest:  path -> collect files -> [workers] hash, skip unchanged, extract, chunk
                                      |
                              bounded queue (32)
                                      |
                       [consumer] batch-embed -> SQLite transaction

query:   question -> embed -> KNN (k = 4) -> stage 1: similarity >= tau_r ?
                                                   |
                       ChatML prompt with the retrieved chunks -> generate
                                                   |
                    stage 2: confidence >= tau_g and no [INSUFFICIENT_DATA] ?
                                                   |
                                      answer + references
```

## Modules

One concern per module: a `.c` in `src/`, its header in `include/`.

| Module | Responsibility |
|---|---|
| `main.c` | CLI router (`setup`, `ingest`, `query`, `chat`, `docs`, `chunks`, `reset`), model download via `curl`, the chat command handlers. |
| `repl.c` | Raw-mode line editor for `chat`: keys, history file, `/command` and path completion with a zsh-style menu. Falls back to plain line input on Windows. |
| `config.c` | `librarian.toml` via tomlc99; local-workspace detection; XDG / AppData / Application Support paths. |
| `db.c` | The only code that includes `sqlite3.h`. Schema, documents, chunks, `vec0` KNN search, FNV-1a content hashes. |
| `doc.c` | Text extraction: plain text and markup, HTML, DOCX, ODT and EPUB in C (miniz for the zip containers), PDF via `pdftotext` when present with a FlateDecode fallback, `.doc` via `antiword`. Binary and Git LFS pointer detection, UTF-8 sanitising. |
| `pipeline.c` | Producer-consumer ingestion: worker threads (all cores, minus two above four) prepare documents, the main thread embeds in batches and writes. |
| `embedder.c` | Sliding-window chunking (words, with overlap), batched embedding through `llama.h`, L2 normalisation. |
| `generator.c` | Prompt assembly, sampling, mean token probability, refusal detection. |
| `ui.c`, `brand.c` | Status lines, progress, references, paginated `docs`/`chunks` listings; the bookshelf banner. |
| `theme.c`, `theme_builtin.c`, `theme_slots.h` | Shared theme engine (byte-identical with redstone) and librarian's slots and built-in themes. |
| `plat_posix.c`, `plat_win32.c`, `plat.h` | Shared platform layer (byte-identical with redstone): terminal, paths, processes, clocks. |
| `logger.c` | Rotating log file under the data directory. |

## Storage

```sql
documents(id, path UNIQUE, content_hash, created_at)
chunks(id, doc_id -> documents ON DELETE CASCADE, chunk_idx, content)
vec_chunks USING vec0(chunk_id PRIMARY KEY, embedding FLOAT[D] distance_metric=cosine)
```

`D` is `[embedder] dimension`. `db_init_schema` is idempotent and runs before
every command that opens the database, so a fresh install works with any of
them. A file whose content hash is unchanged is skipped on re-ingest; a changed
file has its chunks replaced. `reset` drops and recreates the tables.

## Two-stage refusal

1. **Retrieval.** With $d$ the cosine distance of the best match, $S = 1 - d$. If
   $S < \tau_r$ (`similarity_threshold`, default 0.65) the question is refused
   without running the generator.
2. **Generation.** The system prompt tells the model to answer only from the
   supplied context and otherwise reply `[INSUFFICIENT_DATA]`. Confidence is
   the geometric mean token probability
   $C = \exp\big(\tfrac{1}{N}\sum_{i=1}^{N} \ln P(t_i)\big)$; the answer is
   refused if it contains the marker or $C < \tau_g$
   (`confidence_threshold`, default 0.50).

Every answer that passes lists the retrieved chunks as numbered references.

## Concurrency

Only ingestion is concurrent. Workers do I/O and CPU-bound extraction and
never touch SQLite or the model; the single consumer owns both, so neither
needs locking beyond the queue's mutex and condition variables. Embedding
itself is parallel inside `llama.cpp` (OpenMP).

## Platforms

Linux is primary. Windows 10 1809+ builds as a static MinGW-w64 executable;
the differences are confined to `plat_win32.c`, the plain-input REPL fallback,
and `_stat64` for model files over 2 GiB. macOS builds from source.
