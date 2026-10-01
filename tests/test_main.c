#include "logger.h"
#include "minunit.h"

#include <stdio.h>

int tests_run = 0;

/* External test declarations */
extern const char *test_config_defaults(void);
extern const char *test_config_load_file(void);
extern const char *test_ui_badges(void);
extern const char *test_db_lifecycle_and_knn(void);
extern const char *test_chunking_logic(void);
extern const char *test_vector_normalization(void);
extern const char *test_embedder_model(void);
extern const char *test_generator_stage1_refusal(void);
extern const char *test_generator_model_inference(void);
extern const char *test_repl_history_and_lifecycle(void);
extern const char *test_logger_lifecycle(void);

static const char *all_tests(void)
{
    mu_run_test(test_config_defaults);
    mu_run_test(test_config_load_file);
    mu_run_test(test_ui_badges);
    mu_run_test(test_db_lifecycle_and_knn);
    mu_run_test(test_chunking_logic);
    mu_run_test(test_vector_normalization);
    mu_run_test(test_embedder_model);
    mu_run_test(test_generator_stage1_refusal);
    mu_run_test(test_generator_model_inference);
    mu_run_test(test_repl_history_and_lifecycle);
    mu_run_test(test_logger_lifecycle);
    return NULL;
}

int main(void)
{
    logger_init("data/test_run.log");
    printf("--- Running Librarian Unit Tests ---\n");
    const char *result = all_tests();
    if (result != NULL) {
        printf("FAILED: %s\n", result);
        logger_close();
        return 1;
    }
    printf("ALL TESTS PASSED (%d tests run)\n", tests_run);
    logger_close();
    remove("data/test_run.log");
    return 0;
}
