#include "ui.h"
#include <stdio.h>
#include <stdarg.h>

void ui_banner(void)
{
    printf("\n");
    printf(COLOR_PEACH   "  +-----------+\n" COLOR_RESET);
    printf(COLOR_PEACH   " /|   === === | \n" COLOR_RESET);
    printf(COLOR_PEACH   "+-+-----------+\n" COLOR_RESET);
    printf(COLOR_PEACH   "| | [=] [=] [=]|" COLOR_LAVENDER COLOR_BOLD "  L I B R A R I A N\n" COLOR_RESET);
    printf(COLOR_PEACH   "| | [=] [=] [=]|" COLOR_BLUE     "  Your local, self-contained knowledge companion\n" COLOR_RESET);
    printf(COLOR_PEACH   "|/  === ===   |\n" COLOR_RESET);
    printf(COLOR_PEACH   "+-------------+\n\n" COLOR_RESET);
}

void ui_prompt(void)
{
    printf(COLOR_LAVENDER COLOR_BOLD "librarian" COLOR_MINT " ❯ " COLOR_RESET);
    fflush(stdout);
}

void ui_status(const char *status)
{
    printf("\r\x1b[K" COLOR_PEACH "%s" COLOR_RESET, status);
    fflush(stdout);
}

void ui_clear_status(void)
{
    printf("\r\x1b[K");
    fflush(stdout);
}

void ui_confidence_badge(float confidence, bool is_refusal)
{
    int pct = (int)(confidence * 100.0f);
    if (pct < 0) pct = 0;
    if (pct > 100) pct = 100;

    if (is_refusal) {
        printf(COLOR_PEACH "[Refusal Confidence: %d%%]" COLOR_RESET "\n", pct);
    } else {
        printf(COLOR_MINT "[Confidence: %d%%]" COLOR_RESET "\n", pct);
    }
}

void ui_similarity_badge(float similarity, float threshold)
{
    printf(COLOR_LAVENDER "[Similarity: %.3f / min: %.3f]" COLOR_RESET "\n",
           (double)similarity, (double)threshold);
}

void ui_info(const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    printf(COLOR_BLUE "ℹ " COLOR_RESET);
    vprintf(fmt, args);
    printf("\n");
    va_end(args);
}

void ui_success(const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    printf(COLOR_MINT "✓ " COLOR_RESET);
    vprintf(fmt, args);
    printf("\n");
    va_end(args);
}

void ui_warn(const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    printf(COLOR_PEACH "⚠ " COLOR_RESET);
    vprintf(fmt, args);
    printf("\n");
    va_end(args);
}

void ui_error(const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    printf(COLOR_RED "✗ " COLOR_RESET);
    vprintf(fmt, args);
    printf("\n");
    va_end(args);
}
