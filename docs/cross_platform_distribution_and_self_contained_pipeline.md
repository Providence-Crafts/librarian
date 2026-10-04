# Architecture & Design: Cross-Platform Publishing, Self-Contained Ingestion & Interactive Navigation

## Executive Summary

This document sets the technical specification, constraints, tradeoffs, and implementation plan for:
1. **Self-Contained Ingestion**: Eliminating external shell utilities (`pandoc`, `antiword`) and making document handling pure C99 with vendored `miniz` for ZIP archives (DOCX, ODT, EPUB) and in-memory HTML parsing.
2. **Ingestion Stability & Throughput**: Tuning batch sizes to avoid Vulkan/DRM buffer stalls and adding granular chunk-level progress indicators.
3. **Interactive Document/Chunk Navigation**: Implementing a lightweight interactive terminal pager for REPL and CLI modes when browsing large libraries.
4. **Cross-Platform OS Standard Paths**: Supporting `$XDG_CONFIG_HOME` on Linux, `%APPDATA%` on Windows, and `~/Library/Application Support` on macOS.
5. **Distribution & Guided Setup**: Adding a non-destructive, retriggerable `setup` subcommand that automates model downloads from Hugging Face and guides the user through their first session.

---

## 1. Problem Analysis & Root Causes

### 1.1 Ingestion Stalls & DRM Syncobj Hangs
- **Observation**: During ingestion of large books (e.g. 500+ chunks), the progress indicator froze on a single document for minutes.
- **Root Cause**:
  1. `cparams.n_ubatch = 4096` and `cparams.n_batch = 4096` in `embedder.c` forced `llama.cpp` to allocate a 2.6 GB compute buffer on Vulkan.
  2. On AMD integrated APU architectures (e.g. Radeon 860M / RADV), submitting multi-sequence graphs with 4096 tokens in one dispatch causes GPU driver serialization stalls (`drm_syncobj_array_wait`).
  3. The UI progress bar only rendered once per document upon queue completion rather than per batch/chunk, leaving the user with zero visual feedback during the evaluation of hundreds of chunks.

### 1.2 Git LFS Pointer False Failures
- **Observation**: `Could not extract PDF 'Khang Pham - Generative AI System Design Interview (2024).pdf' (pdftotext failed or not installed)`.
- **Root Cause**: The file was a 133-byte Git LFS pointer text file (`version https://git-lfs.github.com/spec/v1`), not a binary PDF. When passed to extraction, external utilities or parsers fail with syntax errors.
- **Solution**: Explicitly inspect file headers for `version https://git-lfs` and skip with an informative notice (`Skipping Git LFS pointer; run 'git lfs pull'`).

### 1.3 External Tool Dependencies
- **Observation**: Relying on `pandoc` (Haskell runtime) or `antiword` breaks the core design philosophy of a self-contained, lightweight C99 application. On Windows and standard Linux machines, users do not have these tools installed.
- **Solution**:
  - HTML (`.html`, `.htm`): Stream-based pure C tag stripper and entity decoder.
  - DOCX (`.docx`): Open with single-file `miniz`, extract `word/document.xml`, parse `<w:t>` and `<w:p>`.
  - ODT (`.odt`): Open with `miniz`, extract `content.xml`, parse `<text:p>`.
  - EPUB (`.epub`): Open with `miniz`, extract XHTML documents and parse via HTML stripper.
  - PDF (`.pdf`): If `pdftotext` is installed, use it; additionally provide stream text extraction fallback in C via `miniz` FlateDecode.

---

## 2. Ingestion Batching & Real-Time Feedback

### 2.1 Compute Buffer Optimization
- Set `n_batch = 2048` and `n_ubatch = 512` in `src/embedder.c`.
- Cap sequence packing at 16 sequences and 2048 tokens per GPU dispatch.
- **Benefit**: Reduces compute buffer from 2,614 MB down to ~280 MB, eliminating Vulkan driver stalls while preserving high throughput (>100 chunks/s).

### 2.2 Live Chunk-Level Progress Bar
- In `src/pipeline.c` and `src/ui.c`, add chunk-level progress reporting:
  ```text
  📦 Ingesting [5/35] ( 14%) • book.pdf [chunk 120/534 - 22% • 135 chunks/s]
  ```
- Commit SQLite transactions periodically (every 10 documents or 200 chunks) to ensure database visibility.

---

## 3. Interactive Navigation (Terminal Pager)

When a user runs `/docs` or `/chunks` in REPL or CLI mode:
- Query results with $> 15$ items should not flood the terminal scrollback.
- Implement `ui_paginate_list()`:
  - Renders 15 items per page with clear header and navigation prompt:
    `--- Page 1 of 8 (Items 1-15 of 120) • [n]ext, [p]rev, [1-8] page, [q]uit ---`
  - In non-interactive mode (pipes, scripts), streams all records without blocking.

---

## 4. Cross-Platform Configuration Paths

| Operating System | Config File Location | Database & Storage Location |
| :--- | :--- | :--- |
| **Linux / BSD** | `$XDG_CONFIG_HOME/librarian/librarian.toml`<br>(fallback: `~/.config/librarian/librarian.toml`) | `$XDG_DATA_HOME/librarian/librarian.db`<br>(fallback: `~/.local/share/librarian/librarian.db`) |
| **macOS** | `~/Library/Application Support/librarian/librarian.toml` | `~/Library/Application Support/librarian/librarian.db` |
| **Windows** | `%APPDATA%\librarian\librarian.toml` | `%LOCALAPPDATA%\librarian\librarian.db` |

*Precedence*: If a `./librarian.toml` exists in the current working directory, it is always loaded first for portable/local execution.

---

## 5. Automated Setup & Guided Walkthrough

### 5.1 Subcommand & Trigger
- Added subcommand `librarian setup` and REPL command `/setup`.
- Automatically invoked when `librarian` is launched without configuration or when model files are missing.

### 5.2 Model Source URLs
- **Embedder**: `https://huggingface.co/BAAI/bge-small-en-v1.5/resolve/main/bge-small-en-v1.5.Q8_0.gguf` or user default `harrier-oss-v1-0.6b.Q8_0.gguf`.
- **Generator**: `https://huggingface.co/openbmb/MiniCPM-2B-dpo-bf16-gguf/resolve/main/MiniCPM-2B-dpo-bf16.Q8_0.gguf` (or compatible MiniCPM5 GGUF).
- Uses `curl -L --progress-bar -C -` to support resumable downloads and standard SSL verification.

### 5.3 Safety
- The setup function never wipes existing databases, chunks, or user documents.
- Prompts for confirmation before downloading files if models already exist.
