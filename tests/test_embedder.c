#include "embedder.h"
#include "minunit.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

const char *test_chunking_logic(void);
const char *test_vector_normalization(void);
const char *test_embedder_model(void);

const char *test_chunking_logic(void)
{
    const char *sample = "The quick brown fox jumps over the lazy dog. "
                         "Pack my box with five dozen liquor jugs. "
                         "How vexingly quick daft zebras jump! "
                         "Bright vixens jump; dozy fowl quack.";

    /* 8 words per chunk, 2 words overlap */
    chunk_list_t list = chunk_text(sample, 8, 2);
    mu_assert("chunk_text failed to produce chunks", list.count > 1);

    for (int i = 0; i < list.count; i++) {
        mu_assert("chunk should not be NULL", list.chunks[i] != NULL);
        mu_assert("chunk should not be empty", strlen(list.chunks[i]) > 0);
    }

    chunk_list_free(&list);
    mu_assert("chunks should be freed", list.chunks == NULL && list.count == 0);

    /* Single word test */
    chunk_list_t single = chunk_text("Hello", 10, 2);
    mu_assert("single word should produce 1 chunk", single.count == 1);
    mu_assert("content should match", strcmp(single.chunks[0], "Hello") == 0);
    chunk_list_free(&single);

    return NULL;
}

const char *test_vector_normalization(void)
{
    float vec[3] = {3.0f, 4.0f, 0.0f};
    vector_normalize_l2(vec, 3);

    mu_assert("norm x component should be 0.6", fabsf(vec[0] - 0.6f) < 1e-5f);
    mu_assert("norm y component should be 0.8", fabsf(vec[1] - 0.8f) < 1e-5f);
    mu_assert("norm z component should be 0.0", fabsf(vec[2] - 0.0f) < 1e-5f);

    float norm_sq = vec[0] * vec[0] + vec[1] * vec[1] + vec[2] * vec[2];
    mu_assert("norm should equal 1.0", fabsf(norm_sq - 1.0f) < 1e-5f);

    return NULL;
}

const char *test_embedder_model(void)
{
    const char *model_path = "models/harrier-oss-v1-0.6b.Q8_0.gguf";
    FILE *f = fopen(model_path, "rb");
    if (!f) {
        printf("  [SKIP] Model %s not found\n", model_path);
        return NULL;
    }
    fclose(f);

    embedder_context_t *emb = embedder_init(model_path, 1024);
    mu_assert("embedder_init failed", emb != NULL);

    float vec[1024] = {0};
    int rc = embedder_embed(emb, "Libraries are quiet places filled with books.", vec);
    mu_assert("embedder_embed failed", rc == 0);

    float sum_sq = 0.0f;
    for (int i = 0; i < 1024; i++) {
        sum_sq += vec[i] * vec[i];
    }
    float norm = sqrtf(sum_sq);
    mu_assert("embedding vector norm should be ~1.0", fabsf(norm - 1.0f) < 1e-3f);

    embedder_free(emb);
    return NULL;
}
