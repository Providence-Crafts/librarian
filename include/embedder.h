#ifndef LIBRARIAN_EMBEDDER_H
#define LIBRARIAN_EMBEDDER_H

#include <stdbool.h>
#include <stddef.h>

typedef struct {
    char **chunks;
    int count;
} chunk_list_t;

/* Chunking text with sliding window */
chunk_list_t chunk_text(const char *text, int chunk_size_words, int overlap_words);
void chunk_list_free(chunk_list_t *list);

typedef struct embedder_context embedder_context_t;

embedder_context_t *embedder_init(const char *model_path, int dimension);
void embedder_free(embedder_context_t *ctx);

int embedder_embed(embedder_context_t *ctx, const char *text, float *out_vec);
void vector_normalize_l2(float *vec, int dim);

#endif /* LIBRARIAN_EMBEDDER_H */
