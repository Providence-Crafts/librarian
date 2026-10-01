#include "logger.h"

#include "ggml.h"
#include "llama.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

static FILE *g_log_fp = NULL;
static char g_log_path[512] = {0};

static void librarian_llama_log_callback(enum ggml_log_level level, const char *text,
                                         void *user_data)
{
    (void)level;
    (void)user_data;
    if (g_log_fp && text) {
        fputs(text, g_log_fp);
        fflush(g_log_fp);
    }
}

int logger_init(const char *log_path)
{
    if (!log_path || log_path[0] == '\0') {
        log_path = "data/librarian.log";
    }

    strncpy(g_log_path, log_path, sizeof(g_log_path) - 1);
    g_log_path[sizeof(g_log_path) - 1] = '\0';

    /* Create parent directory if needed */
    char dir_buf[512];
    strncpy(dir_buf, log_path, sizeof(dir_buf) - 1);
    dir_buf[sizeof(dir_buf) - 1] = '\0';
    char *last_slash = strrchr(dir_buf, '/');
    if (last_slash) {
        *last_slash = '\0';
        mkdir(dir_buf, 0755);
    }

    g_log_fp = fopen(g_log_path, "a");
    if (!g_log_fp) {
        return -1;
    }

    time_t now = time(NULL);
    struct tm tm_buf;
    localtime_r(&now, &tm_buf);
    char time_str[64];
    strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S", &tm_buf);

    fprintf(g_log_fp, "\n=== Librarian Session Started [%s] ===\n", time_str);
    fflush(g_log_fp);

    /* Direct all llama.cpp and ggml internal logs to this file */
    llama_log_set(librarian_llama_log_callback, NULL);
    ggml_log_set(librarian_llama_log_callback, NULL);

    return 0;
}

void logger_log(const char *fmt, ...)
{
    if (!g_log_fp || !fmt) {
        return;
    }

    time_t now = time(NULL);
    struct tm tm_buf;
    localtime_r(&now, &tm_buf);
    char time_str[64];
    strftime(time_str, sizeof(time_str), "%H:%M:%S", &tm_buf);

    fprintf(g_log_fp, "[%s] ", time_str);

    va_list args;
    va_start(args, fmt);
    vfprintf(g_log_fp, fmt, args);
    va_end(args);

    fputc('\n', g_log_fp);
    fflush(g_log_fp);
}

void logger_close(void)
{
    if (g_log_fp) {
        time_t now = time(NULL);
        struct tm tm_buf;
        localtime_r(&now, &tm_buf);
        char time_str[64];
        strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S", &tm_buf);

        fprintf(g_log_fp, "=== Librarian Session Closed [%s] ===\n\n", time_str);
        fflush(g_log_fp);

        llama_log_set(NULL, NULL);
        fclose(g_log_fp);
        g_log_fp = NULL;
    }
}

const char *logger_get_path(void)
{
    return g_log_path;
}
