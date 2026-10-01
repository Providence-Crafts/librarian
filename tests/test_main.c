#include "minunit.h"
#include <stdio.h>

int tests_run = 0;

/* External test declarations */
extern const char *test_config_defaults(void);
extern const char *test_config_load_file(void);
extern const char *test_ui_badges(void);
extern const char *test_db_lifecycle_and_knn(void);

static const char *all_tests(void)
{
    mu_run_test(test_config_defaults);
    mu_run_test(test_config_load_file);
    mu_run_test(test_ui_badges);
    mu_run_test(test_db_lifecycle_and_knn);
    return NULL;
}

int main(void)
{
    printf("--- Running Librarian Unit Tests ---\n");
    const char *result = all_tests();
    if (result != NULL) {
        printf("FAILED: %s\n", result);
        return 1;
    }
    printf("ALL TESTS PASSED (%d tests run)\n", tests_run);
    return 0;
}
