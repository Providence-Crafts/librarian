#ifndef LIBRARIAN_UI_H
#define LIBRARIAN_UI_H

#include <stdbool.h>

/* Modern pastel 24-bit ANSI color escape codes */
#define COLOR_LAVENDER    "\x1b[38;2;189;147;249m"
#define COLOR_MINT        "\x1b[38;2;139;233;180m"
#define COLOR_PEACH       "\x1b[38;2;255;184;108m"
#define COLOR_BLUE        "\x1b[38;2;139;190;255m"
#define COLOR_GRAY        "\x1b[38;2;98;114;164m"
#define COLOR_RED         "\x1b[38;2;255;85;85m"
#define COLOR_BOLD        "\x1b[1m"
#define COLOR_RESET       "\x1b[0m"

void ui_banner(void);
void ui_prompt(void);
void ui_status(const char *status);
void ui_clear_status(void);
void ui_confidence_badge(float confidence, bool is_refusal);
void ui_similarity_badge(float similarity, float threshold);

#if defined(__GNUC__) || defined(__clang__)
#define ATTR_PRINTF(fmt_idx, arg_idx) __attribute__((format(printf, fmt_idx, arg_idx)))
#else
#define ATTR_PRINTF(fmt_idx, arg_idx)
#endif

void ui_info(const char *fmt, ...) ATTR_PRINTF(1, 2);
void ui_success(const char *fmt, ...) ATTR_PRINTF(1, 2);
void ui_warn(const char *fmt, ...) ATTR_PRINTF(1, 2);
void ui_error(const char *fmt, ...) ATTR_PRINTF(1, 2);

#endif /* LIBRARIAN_UI_H */
