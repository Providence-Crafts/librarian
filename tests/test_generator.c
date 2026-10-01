#include "db.h"
#include "generator.h"
#include "minunit.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

const char *test_generator_stage1_refusal(void);
const char *test_generator_model_inference(void);

const char *test_generator_stage1_refusal(void)
{
    /* Test 1: Empty retrieval -> immediate Stage 1 refusal */
    generation_result_t res1 =
        generator_generate(NULL, "What is the secret formula?", NULL, 0, 0.65f, 0.50f);
    mu_assert("res1 should be refusal", res1.is_refusal);
    mu_assert("res1 reason should mention Stage 1", strstr(res1.refusal_reason, "Stage 1") != NULL);
    mu_assert("res1 text should not be null", res1.text != NULL);
    generation_result_free(&res1);

    /* Test 2: Low similarity (0.42 < 0.65) -> immediate Stage 1 refusal */
    search_result_t mock_results[1];
    mock_results[0].chunk_id = 1;
    mock_results[0].doc_id = 1;
    strncpy(mock_results[0].doc_path, "docs/apples.txt", sizeof(mock_results[0].doc_path));
    mock_results[0].content = "Apples are delicious red or green fruits.";
    mock_results[0].distance = 0.58f;
    mock_results[0].similarity = 0.42f;

    generation_result_t res2 =
        generator_generate(NULL, "What is quantum gravity?", mock_results, 1, 0.65f, 0.50f);
    mu_assert("res2 should be refusal", res2.is_refusal);
    mu_assert("res2 reason should mention Stage 1", strstr(res2.refusal_reason, "Stage 1") != NULL);
    mu_assert("res2 confidence should match top similarity", res2.confidence == 0.42f);
    generation_result_free(&res2);

    return NULL;
}

const char *test_generator_model_inference(void)
{
    const char *model_path = "models/MiniCPM5-2B-Q8_0.gguf";
    FILE *f = fopen(model_path, "rb");
    if (!f) {
        printf("  [SKIP] Generator model %s not present or symlink target missing\n", model_path);
        return NULL;
    }
    fclose(f);

    generator_context_t *gen = generator_init(model_path, 2048);
    mu_assert("generator_init failed", gen != NULL);

    search_result_t mock[1];
    mock[0].chunk_id = 1;
    mock[0].doc_id = 1;
    strncpy(mock[0].doc_path, "docs/c99.txt", sizeof(mock[0].doc_path));
    mock[0].content = "The C99 standard introduced line comments starting with double slashes.";
    mock[0].distance = 0.15f;
    mock[0].similarity = 0.85f;

    generation_result_t res =
        generator_generate(gen, "What did C99 introduce?", mock, 1, 0.65f, 0.50f);
    mu_assert("res should not be null", res.text != NULL);
    mu_assert("res confidence should be > 0.0", res.confidence > 0.0f);

    generation_result_free(&res);
    generator_free(gen);
    return NULL;
}
