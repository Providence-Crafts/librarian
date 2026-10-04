#include "brand.h"

#include "brand_art.h"
#include "plat.h"
#include "theme.h"

#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#define BRAND_NAME "librarian"
#define BRAND_TAGLINE "Local RAG knowledge engine"

/* U+25A4 SQUARE WITH HORIZONTAL FILL: a shelf of books, one cell wide. */
#define BRAND_GLYPH "\xe2\x96\xa4"
#define BRAND_ARROW "\xe2\x9d\xaf"

/* Columns between the art and the captions, and the widest caption line:
 * "Pure C99 • local GGUF • v" plus a version of up to a dozen characters. */
#define BRAND_GAP 3u
#define BRAND_CAPTION_W 40u

/* The caption rows, counted in text rows from the top of the art. */
#define BRAND_CAPTION_ROW 4u

static const char g_index[] = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";

/* The same test as redstone's out_utf8_locale(): the first locale variable
 * that is set decides, and the platform default applies when none is. */
static bool utf8_locale(void)
{
    const char *const names[] = {"LC_ALL", "LC_CTYPE", "LANG"};
    size_t i;

    for (i = 0u; i < sizeof(names) / sizeof(names[0]); i++) {
        const char *v = getenv(names[i]);

        if (v != NULL && v[0] != '\0') {
            return strstr(v, "UTF-8") != NULL || strstr(v, "utf8") != NULL ||
                   strstr(v, "UTF8") != NULL || strstr(v, "utf-8") != NULL;
        }
    }
    return plat_utf8_default();
}

static bool truecolor(void)
{
    const char *v = getenv("COLORTERM");

    return plat_truecolor_default() ||
           (v != NULL && (strstr(v, "truecolor") != NULL || strstr(v, "24bit") != NULL));
}

/* The palette entry for pixel (X, Y), or NULL where it is transparent. */
static const unsigned char *pixel(unsigned x, unsigned y)
{
    const char *at;

    if (y >= BRAND_ART_H || g_art[y][x] == '.') {
        return NULL;
    }
    at = strchr(g_index, g_art[y][x]);
    return at != NULL ? g_palette[at - g_index] : NULL;
}

/* One text row of the art: two pixel rows folded into half blocks, upper half
 * as foreground and lower half as background. A transparent half is left to
 * the terminal's own background. */
static void art_row(FILE *out, unsigned row)
{
    unsigned x;

    for (x = 0u; x < BRAND_ART_W; x++) {
        const unsigned char *top = pixel(x, row * 2u);
        const unsigned char *bot = pixel(x, (row * 2u) + 1u);

        if (top != NULL && bot != NULL) {
            fprintf(out, "\x1b[38;2;%u;%u;%um\x1b[48;2;%u;%u;%um\xe2\x96\x80", top[0], top[1],
                    top[2], bot[0], bot[1], bot[2]);
        } else if (top != NULL) {
            fprintf(out, "\x1b[38;2;%u;%u;%um\x1b[49m\xe2\x96\x80", top[0], top[1], top[2]);
        } else if (bot != NULL) {
            fprintf(out, "\x1b[38;2;%u;%u;%um\x1b[49m\xe2\x96\x84", bot[0], bot[1], bot[2]);
        } else {
            fputs("\x1b[0m ", out);
        }
    }
    fputs("\x1b[0m", out);
}

void brand_banner(FILE *out, unsigned cols, const char *version)
{
    const char *reset = theme_sgr(THEME_RESET);
    bool utf8 = utf8_locale();
    bool art =
        theme_colour() && utf8 && truecolor() && cols >= BRAND_ART_W + BRAND_GAP + BRAND_CAPTION_W;
    const char *dot = utf8 ? "\xe2\x80\xa2" : "-";
    unsigned rows = (BRAND_ART_H + 1u) / 2u;
    unsigned r;

    if (!art) {
        fprintf(out, "\n%s%s%s %s%s%s %s\n%s%s%s\n\n", theme_sgr(THEME_BRAND_GLYPH),
                utf8 ? BRAND_GLYPH : "#", reset, theme_sgr(THEME_BRAND_NAME), BRAND_NAME, reset,
                version, theme_sgr(THEME_BRAND_TAGLINE), BRAND_TAGLINE, reset);
        return;
    }
    fputc('\n', out);
    for (r = 0u; r < rows; r++) {
        art_row(out, r);
        if (r == BRAND_CAPTION_ROW) {
            fprintf(out, "%*s%s" BRAND_GLYPH "  L I B R A R I A N%s", (int)BRAND_GAP, "",
                    theme_sgr(THEME_BRAND_NAME), reset);
        } else if (r == BRAND_CAPTION_ROW + 1u) {
            fprintf(out, "%*s%s%s%s", (int)BRAND_GAP, "", theme_sgr(THEME_BRAND_TAGLINE),
                    BRAND_TAGLINE, reset);
        } else if (r == BRAND_CAPTION_ROW + 2u) {
            fprintf(out, "%*s%sPure C99 %s local GGUF %s v%s%s", (int)BRAND_GAP, "",
                    theme_sgr(THEME_BRAND_FACTS), dot, dot, version, reset);
        }
        fputc('\n', out);
    }
    fputc('\n', out);
}

void brand_prompt(char *buf, size_t size)
{
    const char *reset = theme_sgr(THEME_RESET);

    if (size == 0u) {
        return;
    }
    if (!utf8_locale()) {
        (void)snprintf(buf, size, "%s%s%s> ", theme_sgr(THEME_BRAND_NAME), BRAND_NAME, reset);
        return;
    }
    (void)snprintf(buf, size, "%s" BRAND_GLYPH "%s %s%s%s %s" BRAND_ARROW "%s ",
                   theme_sgr(THEME_BRAND_GLYPH), reset, theme_sgr(THEME_BRAND_NAME), BRAND_NAME,
                   reset, theme_sgr(THEME_BRAND_GLYPH), reset);
}
