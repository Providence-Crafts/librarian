#include "theme_slots.h"

#include <stddef.h>

/* clang-format off */
const StyleName theme_names[THEME_STYLE_COUNT] = {
    {NULL,     NULL},         /* RESET */
    {"brand",  "glyph"},
    {"brand",  "name"},
    {"brand",  "tagline"},
    {"brand",  "facts"},
    {"status", "success"},
    {"status", "progress"},
    {"status", "error"},
    {"status", "note"},
    {"status", "info"},
    {"status", "heading"},
    {"menu",   "selected"},
    {"menu",   "match"},
    {"menu",   "detail"},
    {"menu",   "hint"}
};

/* The built-in palettes, in the file format so that the parser is the only
 * thing that turns a colour into an escape sequence.
 *
 * "default" is librarian's own truecolor palette: lavender for the brand,
 * mint, peach and red for outcomes, a muted blue-grey for detail. */
static const char g_default[] =
    "[brand]\n"
    "glyph = #bd93f9\n"
    "name = bold #bd93f9\n"
    "tagline = #8bbeff\n"
    "facts = #6272a4\n"
    "[status]\n"
    "success = #8be9b4\n"
    "progress = #ffb86c\n"
    "error = #ff5555\n"
    "note = #6272a4\n"
    "info = #8bbeff\n"
    "heading = bold #bd93f9\n"
    "[menu]\n"
    "selected = bold #121218 on #bd93f9\n"
    "match = #8c91a5\n"
    "detail = #8bbeff\n"
    "hint = 244\n";

/* The 256-colour cube, for a dark ground without truecolor. */
static const char g_dark[] =
    "[brand]\n"
    "glyph = 141\n"
    "name = bold 141\n"
    "tagline = 111\n"
    "facts = 61\n"
    "[status]\n"
    "success = 115\n"
    "progress = 215\n"
    "error = 203\n"
    "note = 245\n"
    "info = 111\n"
    "heading = bold 141\n"
    "[menu]\n"
    "selected = bold 233 on 141\n"
    "match = 248\n"
    "detail = 111\n"
    "hint = 244\n";

/* Darker tones that keep their contrast on a white ground. */
static const char g_light[] =
    "[brand]\n"
    "glyph = 91\n"
    "name = bold 91\n"
    "tagline = 25\n"
    "facts = 242\n"
    "[status]\n"
    "success = 28\n"
    "progress = 130\n"
    "error = 160\n"
    "note = 242\n"
    "info = 25\n"
    "heading = bold 91\n"
    "[menu]\n"
    "selected = bold 255 on 91\n"
    "match = 238\n"
    "detail = 25\n"
    "hint = 244\n";

/* The eight ANSI colours only, for a terminal with no 256-colour mode. */
static const char g_basic[] =
    "[brand]\n"
    "glyph = magenta\n"
    "name = bold magenta\n"
    "tagline = blue\n"
    "facts = bright black\n"
    "[status]\n"
    "success = green\n"
    "progress = yellow\n"
    "error = bold red\n"
    "note = bright black\n"
    "info = blue\n"
    "heading = bold magenta\n"
    "[menu]\n"
    "selected = reverse\n"
    "match = none\n"
    "detail = blue\n"
    "hint = bright black\n";

const ThemeBuiltin theme_builtins[] = {{"default", g_default},
                                       {"dark", g_dark},
                                       {"light", g_light},
                                       {"basic", g_basic},
                                       {NULL, NULL}};
/* clang-format on */
