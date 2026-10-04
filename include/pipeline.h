#ifndef LIBRARIAN_PIPELINE_H
#define LIBRARIAN_PIPELINE_H

#include "db.h"
#include "embedder.h"

#include <stddef.h>

/**
 * Ingest an array of file paths concurrently using a producer-consumer pipeline:
 * - Worker threads extract text (pdftotext/pandoc/plain), sanitize UTF-8, and chunk.
 * - Consumer thread embeds chunks on the Vulkan GPU and inserts into SQLite in batched
 * transactions.
 */
int pipeline_ingest_files(db_context_t *db, embedder_context_t *emb, char **file_paths,
                          size_t file_count, int embed_dim, int chunk_size, int chunk_overlap,
                          int *total_docs, int *total_chunks, int *total_skipped,
                          int *total_failed);

#endif /* LIBRARIAN_PIPELINE_H */
