#include "logger.h"

#include "ggml.h"
#include "llama.h"

#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <direct.h>
#include <process.h>
#define mkdir_portable(p) _mkdir(p)
#define getpid _getpid
static inline struct tm *portable_localtime_r(const time_t *timer, struct tm *buf)
{
    localtime_s(buf, timer);
    return buf;
}
#define localtime_r(t, b) portable_localtime_r(t, b)
#else
#include <unistd.h>
#define mkdir_portable(p) mkdir(p, 0755)
#endif

#define LOGGER_MAX_FILE_SIZE (10L * 1024L * 1024L) /* 10 MB limit */

static FILE *g_log_fp = NULL;
static char g_log_path[512] = {0};
static log_level_t g_min_level = LOG_LEVEL_DEBUG;
static bool g_console_echo = false;
static pthread_mutex_t g_log_mutex = PTHREAD_MUTEX_INITIALIZER;

static const char *level_to_string(log_level_t level)
{
    switch (level) {
    case LOG_LEVEL_DEBUG:
        return "DEBUG";
    case LOG_LEVEL_INFO:
        return "INFO";
    case LOG_LEVEL_WARN:
        return "WARN";
    case LOG_LEVEL_ERROR:
        return "ERROR";
    default:
        return "INFO";
    }
}

static void logger_check_rotate_locked(void)
{
    if (!g_log_fp) {
        return;
    }

    long pos = ftell(g_log_fp);
    if (pos >= LOGGER_MAX_FILE_SIZE) {
        char rot_path[540];
        (void)snprintf(rot_path, sizeof(rot_path), "%s.1", g_log_path);
        (void)fclose(g_log_fp);
        (void)remove(rot_path);
        (void)rename(g_log_path, rot_path);
        g_log_fp = fopen(g_log_path, "a");
    }
}

static void librarian_llama_log_callback(enum ggml_log_level level, const char *text,
                                         void *user_data)
{
    (void)level;
    (void)user_data;
    if (!text) {
        return;
    }

    (void)pthread_mutex_lock(&g_log_mutex);
    if (g_console_echo) {
        fputs(text, stderr);
        (void)fflush(stderr);
    }
    if (g_log_fp) {
        logger_check_rotate_locked();
        if (g_log_fp) {
            fputs(text, g_log_fp);
            (void)fflush(g_log_fp);
        }
    }
    (void)pthread_mutex_unlock(&g_log_mutex);
}

int logger_init(const char *log_path)
{
    (void)pthread_mutex_lock(&g_log_mutex);

    if (g_log_fp) {
        (void)fclose(g_log_fp);
        g_log_fp = NULL;
    }

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
#if defined(_WIN32)
    if (!last_slash) last_slash = strrchr(dir_buf, '\\');
#endif
    if (last_slash) {
        *last_slash = '\0';
        (void)mkdir_portable(dir_buf);
    }

    g_log_fp = fopen(g_log_path, "a");
    if (!g_log_fp) {
        (void)pthread_mutex_unlock(&g_log_mutex);
        return -1;
    }

    struct timespec ts;
    (void)clock_gettime(CLOCK_REALTIME, &ts);
    struct tm tm_buf;
    localtime_r(&ts.tv_sec, &tm_buf);
    char time_str[64];
    strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S", &tm_buf);

    fprintf(g_log_fp, "\n=== Librarian Session Started [%s.%03ld] (PID %d) ===\n", time_str,
            ts.tv_nsec / 1000000L, (int)getpid());
    (void)fflush(g_log_fp);

    /* Direct all llama.cpp and ggml internal logs to this file */
    llama_log_set(librarian_llama_log_callback, NULL);
    ggml_log_set(librarian_llama_log_callback, NULL);

    (void)pthread_mutex_unlock(&g_log_mutex);
    return 0;
}

void logger_set_level(log_level_t level)
{
    (void)pthread_mutex_lock(&g_log_mutex);
    g_min_level = level;
    (void)pthread_mutex_unlock(&g_log_mutex);
}

log_level_t logger_get_level(void)
{
    (void)pthread_mutex_lock(&g_log_mutex);
    log_level_t lvl = g_min_level;
    (void)pthread_mutex_unlock(&g_log_mutex);
    return lvl;
}

void logger_set_console_echo(bool enable)
{
    (void)pthread_mutex_lock(&g_log_mutex);
    g_console_echo = enable;
    if (enable) {
        g_min_level = LOG_LEVEL_DEBUG;
    }
    (void)pthread_mutex_unlock(&g_log_mutex);
}

bool logger_get_console_echo(void)
{
    (void)pthread_mutex_lock(&g_log_mutex);
    bool echo = g_console_echo;
    (void)pthread_mutex_unlock(&g_log_mutex);
    return echo;
}

void logger_log_level(log_level_t level, const char *fmt, ...)
{
    if (level < g_min_level || !fmt) {
        return;
    }

    (void)pthread_mutex_lock(&g_log_mutex);

    if (g_console_echo) {
        fprintf(stderr, "\x1b[38;2;98;114;164m[%s] \x1b[0m", level_to_string(level));
        va_list args_console;
        va_start(args_console, fmt);
        vfprintf(stderr, fmt, args_console);
        va_end(args_console);
        fputc('\n', stderr);
        (void)fflush(stderr);
    }

    if (!g_log_fp) {
        (void)pthread_mutex_unlock(&g_log_mutex);
        return;
    }

    logger_check_rotate_locked();
    if (!g_log_fp) {
        (void)pthread_mutex_unlock(&g_log_mutex);
        return;
    }

    struct timespec ts;
    (void)clock_gettime(CLOCK_REALTIME, &ts);
    struct tm tm_buf;
    localtime_r(&ts.tv_sec, &tm_buf);
    char time_str[64];
    strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S", &tm_buf);

    fprintf(g_log_fp, "[%s.%03ld] [%-5s] ", time_str, ts.tv_nsec / 1000000L,
            level_to_string(level));

    va_list args;
    va_start(args, fmt);
    vfprintf(g_log_fp, fmt, args);
    va_end(args);

    fputc('\n', g_log_fp);
    if (level >= LOG_LEVEL_WARN) {
        (void)fflush(g_log_fp);
    }

    (void)pthread_mutex_unlock(&g_log_mutex);
}

void logger_log(const char *fmt, ...)
{
    if (!fmt) {
        return;
    }

    va_list args;
    va_start(args, fmt);

    char buf[2048];
    (void)vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);

    logger_log_level(LOG_LEVEL_INFO, "%s", buf);
}

void logger_debug(const char *fmt, ...)
{
    if (!fmt)
        return;
    va_list args;
    va_start(args, fmt);
    char buf[2048];
    (void)vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    logger_log_level(LOG_LEVEL_DEBUG, "%s", buf);
}

void logger_info(const char *fmt, ...)
{
    if (!fmt)
        return;
    va_list args;
    va_start(args, fmt);
    char buf[2048];
    (void)vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    logger_log_level(LOG_LEVEL_INFO, "%s", buf);
}

void logger_warn(const char *fmt, ...)
{
    if (!fmt)
        return;
    va_list args;
    va_start(args, fmt);
    char buf[2048];
    (void)vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    logger_log_level(LOG_LEVEL_WARN, "%s", buf);
}

void logger_error(const char *fmt, ...)
{
    if (!fmt)
        return;
    va_list args;
    va_start(args, fmt);
    char buf[2048];
    (void)vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    logger_log_level(LOG_LEVEL_ERROR, "%s", buf);
}

void logger_flush(void)
{
    (void)pthread_mutex_lock(&g_log_mutex);
    if (g_log_fp) {
        (void)fflush(g_log_fp);
    }
    (void)pthread_mutex_unlock(&g_log_mutex);
}

void logger_close(void)
{
    (void)pthread_mutex_lock(&g_log_mutex);
    if (g_log_fp) {
        struct timespec ts;
        (void)clock_gettime(CLOCK_REALTIME, &ts);
        struct tm tm_buf;
        localtime_r(&ts.tv_sec, &tm_buf);
        char time_str[64];
        strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S", &tm_buf);

        fprintf(g_log_fp, "=== Librarian Session Closed [%s.%03ld] ===\n\n", time_str,
                ts.tv_nsec / 1000000L);
        (void)fflush(g_log_fp);

        llama_log_set(NULL, NULL);
        ggml_log_set(NULL, NULL);
        (void)fclose(g_log_fp);
        g_log_fp = NULL;
    }
    (void)pthread_mutex_unlock(&g_log_mutex);
}

const char *logger_get_path(void)
{
    return g_log_path;
}
