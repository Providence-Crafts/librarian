# Ingestion Throughput Acceleration Analysis

## Executive Summary

During Phase 9 and 10 testing on a 300-page academic textbook (*"Machine Learning Control"*, 80,058 words), ingestion required **265.62 seconds (4.42 minutes)** to extract, chunk, and embed **798 chunks** across 12 OpenMP CPU threads (~3.0 chunks/second). Re-ingestion of unchanged documents takes **0.01 seconds** via 64-bit streaming FNV-1a hash deduplication.

Profiling indicates that **99.8% of ingestion time is spent in the transformer forward pass (`llama_decode`)**. To accelerate initial library ingestion across hundreds of books, several complementary levers exist.

---

## Performance Bottleneck Breakdown

```
[ Ingestion Pipeline Timing for 80,058-word PDF ]
┌─────────────────────────┬──────────────┬─────────────┐
│ Stage                   │ Time (s)     │ % of Total  │
├─────────────────────────┼──────────────┼─────────────┤
│ File Hash (FNV-1a64)    │ 0.005 s      │ 0.002 %     │
│ PDF Text Extraction     │ 0.564 s      │ 0.21 %      │
│ Word Tokenizer/Chunking │ 0.004 s      │ 0.001 %     │
│ Embedding Inference     │ 265.05 s     │ 99.78 %     │
│ SQLite Insertion        │ 0.012 s      │ 0.005 %     │
└─────────────────────────┴──────────────┴─────────────┘
```

The sole performance bottleneck is the number of tokens decoded through the transformer model on CPU.

---

## Acceleration Strategies & Tradeoffs

### Strategy 1: Optimal Chunk Geometry (2.5x Speedup, Zero New Dependencies)

- **Mechanism:** Increase chunk size from 120 words (~160 tokens) to 250–300 words (~350–400 tokens) with 40–50 words overlap.
- **Impact:**
  - 80,000 words @ 120-word chunk (step 100) = **798 chunks** (265 s).
  - 80,000 words @ 250-word chunk (step 210) = **381 chunks** (~125 s, **2.1x faster**).
  - 80,000 words @ 300-word chunk (step 250) = **320 chunks** (~105 s, **2.5x faster**).
- **RAG Quality Impact:**
  - Harrier-OSS-v1-0.6B has a 4,096-token context window; 120 words (~160 tokens) severely underutilizes its attention span.
  - 250–300 words encompasses complete conceptual paragraphs and proofs rather than fragmented sentences, typically improving cosine retrieval accuracy in technical literature.
- **Configurability:** Expose `chunk_size_words` and `chunk_overlap_words` in `librarian.toml` so users can tune granularity.

### Strategy 2: Micro-Batch & Vector Alignment Tuning (1.2x – 1.3x Speedup)

- **Mechanism:**
  - Increase micro-batch (`cparams.n_ubatch`) from `512` to `2048` in `embedder.c`.
  - When `n_ubatch = 512`, GGML splits the batch into small sub-computations, causing thread barrier stalls and lower cache reuse.
  - Setting `n_ubatch = 2048` expands the inner matrix multiplication dimension ($M \times K$), allowing OpenMP threads to maintain higher arithmetic intensity on AVX2 vector units.
- **Impact:** ~15% to 25% faster token processing with zero code complexity.

### Strategy 3: Asynchronous Extraction Pipeline (Overlapping I/O & Compute)

- **Mechanism:**
  - Implement a 2-stage producer-consumer pipeline: while the embedding worker decodes Document $N$ on 12 CPU threads, a background worker extracts text and computes hashes for Document $N+1$ and $N+2$.
- **Impact:**
  - For small documents (< 5 pages), hides process spawning and I/O latency completely.
  - Modest overall impact for large books since CPU embedding dominates >99% of time.

### Strategy 4: Vulkan GPU Compute Offloading (10x – 15x Speedup)

- **Mechanism:**
  - Offload transformer layers to the host GPU via `ggml-vulkan`.
  - The system hardware features an **AMD Radeon 840M / 860M Graphics APU** with Vulkan 1.4 support.
  - Offloading 24 transformer layers to Vulkan can reduce 798 chunks from 265s to **~18–25s**.
- **Tradeoffs:**
  - Requires adding `vulkan-headers`, `vulkan-loader`, and `shaderc` to `flake.nix`.
  - Requires linking `libvulkan.so.1`.
  - Breaks the pure-CPU zero-dependency guarantee of the base build (could be an optional make target `make vk`).

---

## Strategy Comparison Matrix

| Strategy | Ingestion Speedup | Implementation Effort | External Dependencies | Impact on Search Quality |
| :--- | :--- | :--- | :--- | :--- |
| **1. Chunk Geometry (250–300 words)** | **2.5x** | Low (config + main) | None | **Positive** (richer context) |
| **2. Micro-Batch Tuning (`n_ubatch=2048`)** | **1.2x – 1.3x** | Very Low (1 line) | None | Neutral (identical vectors) |
| **3. Multi-Document Pipeline** | **1.1x** | Medium | None (POSIX pthread) | Neutral |
| **4. Vulkan GPU Offloading** | **10x – 15x** | Medium | Vulkan SDK / Drivers | Neutral |

---

## Recommended Action Plan

1. **Immediate High-Value Step (Zero Risk, ~3.0x Combined Speedup):**
   - Tune chunk size to **250 words** with **40 words overlap** (configurable in `librarian.toml`).
   - Increase `cparams.n_ubatch` to `2048` in `src/embedder.c`.
   - Combined effect: Reduces a 300-page book from **265s down to ~85s** on CPU.
2. **Optional Next Phase:**
   - Evaluate Vulkan GPU build flag (`make vulkan`) for users seeking 15x acceleration on supported AMD/Intel/NVIDIA GPUs.
