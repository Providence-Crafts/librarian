#include "minunit.h"
#include "repl.h"

#include <stdio.h>
#include <string.h>
#include <unistd.h>

const char *test_repl_history_and_lifecycle(void);

const char *test_repl_history_and_lifecycle(void)
{
    const char *test_path = "data/test_history.txt";
    unlink(test_path);

    repl_context_t *repl = repl_init(test_path);
    mu_assert("repl_init should return non-null context", repl != NULL);

    repl_history_add(repl, "/help");
    repl_history_add(repl, "/stats");
    repl_history_add(repl, "/stats"); /* duplicate, should not add twice */
    repl_history_add(repl, "What is Librarian?");

    repl_free(repl);

    /* Verify persistence across reloads */
    repl_context_t *reloaded = repl_init(test_path);
    mu_assert("reloaded repl should not be null", reloaded != NULL);
    mu_assert("reloaded history_count should be 3", repl_history_count(reloaded) == 3);

    repl_free(reloaded);

    unlink(test_path);
    return NULL;
}
