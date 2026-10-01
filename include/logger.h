#ifndef LIBRARIAN_LOGGER_H
#define LIBRARIAN_LOGGER_H

#include <stdbool.h>

#if defined(__GNUC__) || defined(__clang__)
#define ATTR_LOGGER_PRINTF(fmt_idx, arg_idx) __attribute__((format(printf, fmt_idx, arg_idx)))
#else
#define ATTR_LOGGER_PRINTF(fmt_idx, arg_idx)
#endif

int logger_init(const char *log_path);
void logger_log(const char *fmt, ...) ATTR_LOGGER_PRINTF(1, 2);
void logger_close(void);
const char *logger_get_path(void);

#endif /* LIBRARIAN_LOGGER_H */
