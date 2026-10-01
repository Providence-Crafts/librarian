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

    logger_set_level(LOG_LEVEL_INFO);
    mu_assert("logger_get_level should be INFO", logger_get_level() == LOG_LEVEL_INFO);

    logger_debug("This debug message should be filtered out");
    logger_info("Info message: %s %d", "hello", 123);
    logger_warn("Warning condition: %s", "check disk");
    logger_error("Error event: code %d", 500);
    logger_flush();
    logger_close();

    FILE *fp = fopen(test_path, "r");
    mu_assert("log file should exist after closing", fp != NULL);

    char content[2048] = {0};
    size_t bytes = fread(content, 1, sizeof(content) - 1, fp);
    fclose(fp);

    mu_assert("log file should not be empty", bytes > 0);
    mu_assert("log file should not contain filtered debug message",
              strstr(content, "This debug message should be filtered out") == NULL);
    mu_assert("log file should contain info message",
              strstr(content, "Info message: hello 123") != NULL);
    mu_assert("log file should contain warn message",
              strstr(content, "Warning condition: check disk") != NULL);
    mu_assert("log file should contain error message",
              strstr(content, "Error event: code 500") != NULL);
    mu_assert("log file should contain session started",
              strstr(content, "Librarian Session Started") != NULL);
    mu_assert("log file should contain session closed",
              strstr(content, "Librarian Session Closed") != NULL);

    unlink(test_path);
    return NULL;
}
