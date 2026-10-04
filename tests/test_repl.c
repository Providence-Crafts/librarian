#include "minunit.h"
#include "repl.h"

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

const char *test_repl_history_and_lifecycle(void);
const char *test_repl_long_line_history_roundtrip(void);

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

/* A line longer than the initial buffer, then Up and Down: the line is saved
 * to the scratch buffer and restored. The scratch buffer once kept its initial
 * size, so this overflowed the heap (caught under ASan). */
const char *test_repl_long_line_history_roundtrip(void)
{
    int master = posix_openpt(O_RDWR | O_NOCTTY);
    mu_assert("posix_openpt should succeed", master >= 0);
    mu_assert("grantpt/unlockpt should succeed", grantpt(master) == 0 && unlockpt(master) == 0);
    int slave = open(ptsname(master), O_RDWR | O_NOCTTY);
    mu_assert("pty slave should open", slave >= 0);

    char keys[300 + 8];
    memset(keys, 'a', 300);
    memcpy(keys + 300, "\x1b[A\x1b[B\r", 8);
    mu_assert("keys should be queued",
              write(master, keys, sizeof(keys) - 1) == (ssize_t)sizeof(keys) - 1);

    int saved_in = dup(STDIN_FILENO);
    int saved_out = dup(STDOUT_FILENO);
    int devnull = open("/dev/null", O_WRONLY);
    mu_assert("descriptors should be available", saved_in >= 0 && saved_out >= 0 && devnull >= 0);
    (void)fflush(stdout);
    (void)dup2(slave, STDIN_FILENO);
    (void)dup2(devnull, STDOUT_FILENO);

    const char *hist = "data/test_history_long.txt";
    unlink(hist);
    repl_context_t *repl = repl_init(hist);
    char *line = NULL;
    size_t len = 0;
    int all_a = 0;
    if (repl) {
        repl_history_add(repl, "short");
        line = repl_readline(repl, "> ");
        len = line ? strlen(line) : 0;
        all_a = line && strspn(line, "a") == 300;
    }

    (void)fflush(stdout);
    (void)dup2(saved_in, STDIN_FILENO);
    (void)dup2(saved_out, STDOUT_FILENO);
    close(saved_in);
    close(saved_out);
    close(devnull);
    close(slave);
    close(master);
    repl_free(repl);
    unlink(hist);

    mu_assert("repl_init should succeed", repl != NULL);
    mu_assert("the long line should survive Up then Down", len == 300 && all_a);
    return NULL;
}
