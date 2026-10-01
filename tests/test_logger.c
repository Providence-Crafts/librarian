#include "logger.h"
#include "minunit.h"

#include <stdio.h>
#include <string.h>
#include <unistd.h>

const char *test_logger_lifecycle(void);

const char *test_logger_lifecycle(void)
{
    const char *test_path = "data/test_logger.log";
    unlink(test_path);

    int rc = logger_init(test_path);
    mu_assert("logger_init should return 0", rc == 0);

    logger_log("Test message: %s %d", "hello", 123);
    logger_close();

    FILE *fp = fopen(test_path, "r");
    mu_assert("log file should exist after closing", fp != NULL);

    char content[512] = {0};
    size_t bytes = fread(content, 1, sizeof(content) - 1, fp);
    fclose(fp);

    mu_assert("log file should not be empty", bytes > 0);
    mu_assert("log file should contain test message",
              strstr(content, "Test message: hello 123") != NULL);
    mu_assert("log file should contain session started",
              strstr(content, "Librarian Session Started") != NULL);
    mu_assert("log file should contain session closed",
              strstr(content, "Librarian Session Closed") != NULL);

    unlink(test_path);
    return NULL;
}
