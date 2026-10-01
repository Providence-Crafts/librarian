#ifndef LIBRARIAN_GENERATOR_H
#define LIBRARIAN_GENERATOR_H

#include "db.h"

#include <stdbool.h>

#define REFUSAL_INSUFFICIENT_DATA_TOKEN "[INSUFFICIENT_DATA]"

typedef struct generator_context generator_context_t;

typedef struct {
    char *text;
    float confidence; /* mean token exp(logprob) */
    bool is_refusal;
    char refusal_reason[128];
} generation_result_t;

generator_context_t *generator_init(const char *model_path, int context_length);
void generator_free(generator_context_t *ctx);

generation_result_t generator_generate(generator_context_t *ctx, const char *question,
                                       const search_result_t *retrieved, int retrieved_count,
                                       float similarity_threshold, float confidence_threshold);

void generation_result_free(generation_result_t *res);

#endif /* LIBRARIAN_GENERATOR_H */
