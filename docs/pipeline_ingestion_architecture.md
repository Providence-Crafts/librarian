# Pipelined Multi-Document Parallel Ingestion Architecture

## Executive Summary

Currently, Librarian ingests files sequentially:
`File N: [Hash] -> [pdftotext/pandoc Extract] -> [Chunk] -> [GPU Embed Chunks] -> [DB Insert] -> File N+1`

While embedding chunks of a single file uses OpenMP and Vulkan GPU, **text extraction is single-threaded and runs sequentially**. During the 1–5 seconds `pdftotext` takes to parse a 300-page PDF, the GPU and remaining CPU cores sit idle.

To achieve maximum possible throughput, we introduce a **Pipelined Producer-Consumer Architecture**:
- **Producers (CPU Thread Pool):** $P$ worker threads concurrently hash, extract (`pdftotext`/`pandoc`), sanitize UTF-8, and chunk multiple files in parallel.
- **Consumer (GPU Embedding & Storage):** The main thread aggregates chunks across multiple files into saturated GPU batches (e.g., 32–64 chunks per batch), computes vector embeddings on the Vulkan GPU, and writes them to SQLite in batched transactions.

```
┌─────────────────────────────────────────────────────────────┐
│                   File Queue (All Files)                    │
└─────────────────────────────────────────────────────────────┘
         │                   │                   │
         ▼                   ▼                   ▼
   ┌───────────┐       ┌───────────┐       ┌───────────┐
   │ Worker 1  │       │ Worker 2  │       │ Worker P  │  (Concurrent CPU Extract
   │ pdftotext │       │  pandoc   │       │ Read / MD │   & Sliding Chunker)
   └───────────┘       └───────────┘       └───────────┘
         │                   │                   │
         └───────────────────┼───────────────────┘
                             ▼
┌─────────────────────────────────────────────────────────────┐
│           Bounded Document Chunk Queue (Thread-Safe)        │
└─────────────────────────────────────────────────────────────┘
                             │
                             ▼
┌─────────────────────────────────────────────────────────────┐
│              Consumer (GPU & SQLite Writer)                 │
│  - Pack cross-document chunks into large GPU batches        │
│  - Execute embedder_embed_batch on Vulkan GPU               │
│  - Insert chunks into SQLite (batched transactions)         │
│  - Update pastel progress indicator                         │
└─────────────────────────────────────────────────────────────┘
```

---

## Performance Analysis & Expected Gains

| Subsystem | Current Sequential Architecture | Pipelined Concurrent Architecture | Expected Speedup |
| :--- | :--- | :--- | :--- |
| **File Hashing & Skip Check** | Sequential (1 thread) | Parallel ($P$ threads) | **$4\times$–$8\times$** |
| **PDF / EPUB Extraction** | Sequential, blocking GPU | Concurrent background workers | **$3\times$–$6\times$** |
| **GPU Utilization** | Burst-idle (stalled on I/O) | Continuous near-100% saturation | **$2\times$–$3\times$** |
| **Small Document Batching** | 1 file per batch (sub-optimal) | Multi-file chunk aggregation | **$1.5\times$–$2.5\times$** |
| **Overall Ingestion Throughput** | ~1–3 docs/sec (PDF heavy) | **~8–15 docs/sec** | **$\approx 3\times$–$5\times$ overall** |

---

## Architectural Components

### 1. Document Extraction Queue Item
```c
typedef struct {
    char *path;
    char hash_str[32];
    bool is_duplicate;
    bool is_unchanged;
    int64_t duplicate_doc_id;
    chunk_list_t chunks; /* Ready-to-embed chunks */
} prepared_doc_t;
```

### 2. Thread-Safe Bounded Queue
- Backed by standard POSIX `pthread_mutex_t` and `pthread_cond_t` (`cond_not_empty`, `cond_not_full`).
- Bounded capacity (e.g., 32 documents) to prevent unbounded memory growth if extraction runs faster than GPU embedding.
- When full, worker threads block until the GPU consumer drains items.

### 3. Worker Thread Pool
- Number of workers: $P = \min(8, \max(2, \text{nproc} - 4))$.
- Each worker picks the next file path atomically via an atomic index or mutex.
- Fast hash check:
  - If file is unchanged, mark `is_unchanged = true` and enqueue immediately without running `pdftotext`.
  - If duplicate hash, mark `is_duplicate = true` and enqueue without running extraction.
  - If fresh, extract text via `doc_extract_text`, sanitize, and chunk.
- Pushes `prepared_doc_t` to the bounded queue.

### 4. GPU Embedding & SQLite Consumer
- Drains `prepared_doc_t` from the queue.
- If `is_unchanged`: increments skipped count, updates progress bar.
- If `is_duplicate`: links document in SQLite, updates progress bar.
- If fresh chunks:
  - Aggregates chunks from one or more documents into an embedding batch.
  - Calls `embedder_embed_batch(emb, chunks, vecs, count)`.
  - Writes to SQLite within a batched transaction (commit every 50 docs).
  - Updates progress indicator smoothly.

---

## Constraints & Tradeoffs

1. **Memory Footprint:**
   - *Constraint:* Unbounded queues could cause excessive RAM usage with hundreds of extracted books in memory.
   - *Mitigation:* Fixed queue capacity of 16–32 documents (~50–100 MB max queue footprint).
2. **SQLite Thread Affinity:**
   - *Constraint:* SQLite writes are single-threaded and lock-sensitive under WAL mode.
   - *Mitigation:* Only the consumer thread writes to SQLite. Worker threads perform zero SQLite writes.
3. **GPU Context Concurrency:**
   - *Constraint:* `llama_context` in llama.cpp is not thread-safe for concurrent evaluation.
   - *Mitigation:* Only the consumer thread interacts with `embedder_context_t`, ensuring 100% thread safety and zero lock contention on the GPU.
