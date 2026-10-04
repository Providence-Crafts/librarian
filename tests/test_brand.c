/*
 * Tests for brand.c and ui_printf: the banner, the prompt and style tokens.
 *
 * The banner depends on four things outside it -- the theme's colour switch,
 * the locale, COLORTERM and the terminal width -- so each test sets all the
 * ones it cares about, and theme colour is switched off again at the end.
 */
#include "brand.h"
#include "minunit.h"
#include "plat.h"
#include "theme.h"
#include "ui.h"

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

const char *test_brand_prompt(void);
const char *test_brand_banner_gating(void);
const char *test_ui_printf_tokens(void);

#define BUF_SIZE 32768u

static char g_buf[BUF_SIZE];

/* Run the banner into g_buf. Returns the byte count, 0 on failure. */
static size_t capture_banner(unsigned cols)
{
    FILE *tmp = tmpfile();
    size_t n;

    if (tmp == NULL) {
        return 0u;
    }
    brand_banner(tmp, cols, "9.9.9");
    n = (size_t)ftell(tmp);
    if (n >= BUF_SIZE || fseek(tmp, 0L, SEEK_SET) != 0) {
        (void)fclose(tmp);
        return 0u;
    }
    n = fread(g_buf, 1u, n, tmp);
    g_buf[n] = '\0';
    (void)fclose(tmp);
    return n;
}

static void set_env(bool utf8, const char *colorterm, bool colour)
{
    (void)plat_setenv("LC_ALL", utf8 ? "en_US.UTF-8" : "C");
    if (colorterm != NULL) {
        (void)plat_setenv("COLORTERM", colorterm);
    } else {
        (void)plat_unsetenv("COLORTERM");
    }
    theme_reset();
    theme_set_colour(colour);
}

/* Terminal cells taken by S: escape sequences take none, and every other code
 * point in the prompt is a single-cell character. */
static size_t cells(const char *s)
{
    size_t n = 0u;

    while (*s != '\0') {
        if (*s == '\x1b') {
            while (*s != '\0' && *s != 'm') {
                s++;
            }
            if (*s != '\0') {
                s++;
            }
            continue;
        }
        if (((unsigned char)*s & 0xC0u) != 0x80u) {
            n++;
        }
        s++;
    }
    return n;
}

const char *test_brand_prompt(void)
{
    char buf[160];

    set_env(true, NULL, true);
    brand_prompt(buf, sizeof(buf));
    /* U+25A4, three bytes and one cell: a byte count would misplace the cursor */
    mu_assert("prompt starts with the bookshelf glyph", strstr(buf, "\xe2\x96\xa4") != NULL);
    mu_assert("prompt names the program", strstr(buf, "librarian") != NULL);
    mu_assert("prompt ends with the arrow and a space", strstr(buf, "\xe2\x9d\xaf") != NULL);
    mu_assert("prompt is 14 cells: glyph, name, arrow, spaces", cells(buf) == 14u);

    set_env(true, NULL, false);
    brand_prompt(buf, sizeof(buf));
    mu_assert("no colour means no escapes", strchr(buf, '\x1b') == NULL);
    mu_assert("plain UTF-8 prompt is exact",
              strcmp(buf, "\xe2\x96\xa4 librarian \xe2\x9d\xaf ") == 0);

    set_env(false, NULL, false);
    brand_prompt(buf, sizeof(buf));
    mu_assert("non-UTF-8 prompt is ASCII", strcmp(buf, "librarian> ") == 0);

    brand_prompt(buf, 4u);
    mu_assert("a short buffer is still terminated", strlen(buf) == 3u);
    theme_set_colour(false);
    return NULL;
}

const char *test_brand_banner_gating(void)
{
    set_env(true, "truecolor", true);
    mu_assert("wide truecolor banner is written", capture_banner(120u) > 0u);
    mu_assert("art is drawn in half blocks", strstr(g_buf, "\xe2\x96\x80") != NULL);
    mu_assert("caption spells the name out", strstr(g_buf, "L I B R A R I A N") != NULL);
    mu_assert("facts carry the version", strstr(g_buf, "v9.9.9") != NULL);

    set_env(true, "truecolor", true);
    mu_assert("narrow banner is written", capture_banner(40u) > 0u);
    mu_assert("too narrow: no art", strstr(g_buf, "\xe2\x96\x80") == NULL);
    mu_assert("too narrow: captions remain",
              strstr(g_buf, "librarian") != NULL && strstr(g_buf, " 9.9.9") != NULL);

    set_env(true, NULL, true);
    (void)capture_banner(120u);
    mu_assert("no truecolor: no art", strstr(g_buf, "\xe2\x96\x80") == NULL);

    set_env(true, "truecolor", false);
    (void)capture_banner(120u);
    mu_assert("no colour: no art", strstr(g_buf, "\xe2\x96\x80") == NULL);
    mu_assert("no colour: no escapes", strchr(g_buf, '\x1b') == NULL);

    set_env(false, "truecolor", true);
    (void)capture_banner(120u);
    mu_assert("no UTF-8: no art", strstr(g_buf, "\xe2\x96\x80") == NULL);
    mu_assert("no UTF-8: ASCII glyph",
              strchr(g_buf, '#') != NULL && strstr(g_buf, "\xe2\x96\xa4") == NULL);
    theme_set_colour(false);
    return NULL;
}

const char *test_ui_printf_tokens(void)
{
    FILE *saved = stdout;
    FILE *tmp = tmpfile();
    size_t n;

    mu_assert("tmpfile", tmp != NULL);
    stdout = tmp; /* NOLINT: glibc's stdout is assignable; restored below */
    theme_reset();
    theme_set_colour(false);
    (void)ui_printf(STYLE_HEADING "a%d" STYLE_RESET "\n", 1);
    theme_set_colour(true);
    (void)ui_printf(STYLE_ERROR "b" STYLE_RESET "\n");
    theme_set_colour(false);
    fflush(tmp);
    stdout = saved;

    n = (size_t)ftell(tmp);
    mu_assert("output fits", n < BUF_SIZE && fseek(tmp, 0L, SEEK_SET) == 0);
    n = fread(g_buf, 1u, n, tmp);
    g_buf[n] = '\0';
    (void)fclose(tmp);
    mu_assert("tokens never reach the terminal", strchr(g_buf, '\x0e') == NULL);
    mu_assert("colour off: tokens vanish", strncmp(g_buf, "a1\n", 3) == 0);
    mu_assert("colour on: the error style is emitted", strstr(g_buf, "\x1b[") != NULL);
    return NULL;
}
