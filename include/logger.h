#ifndef LIBRARIAN_LOGGER_H
#define LIBRARIAN_LOGGER_H

#include <stdbool.h>

typedef enum {
    LOG_LEVEL_DEBUG = 0,
    LOG_LEVEL_INFO = 1,
    LOG_LEVEL_WARN = 2,
    LOG_LEVEL_ERROR = 3
} log_level_t;

#if defined(__GNUC__) || defined(__clang__)
#define ATTR_LOGGER_PRINTF(fmt_idx, arg_idx) __attribute__((format(printf, fmt_idx, arg_idx)))
#else
#define ATTR_LOGGER_PRINTF(fmt_idx, arg_idx)
#endif

int logger_init(const char *log_path);
void logger_set_level(log_level_t level);
log_level_t logger_get_level(void);

void logger_log_level(log_level_t level, const char *fmt, ...) ATTR_LOGGER_PRINTF(2, 3);
void logger_log(const char *fmt, ...) ATTR_LOGGER_PRINTF(1, 2);

void logger_debug(const char *fmt, ...) ATTR_LOGGER_PRINTF(1, 2);
void logger_info(const char *fmt, ...) ATTR_LOGGER_PRINTF(1, 2);
void logger_warn(const char *fmt, ...) ATTR_LOGGER_PRINTF(1, 2);
void logger_error(const char *fmt, ...) ATTR_LOGGER_PRINTF(1, 2);

void logger_flush(void);
void logger_close(void);
const char *logger_get_path(void);

#endif /* LIBRARIAN_LOGGER_H */
