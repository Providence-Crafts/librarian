/*
 * theme_slots.h - librarian's styles, for the shared theme engine.
 *
 * theme.c and theme.h are shared byte for byte with redstone; what differs per
 * program is this list of styles, where each is written in a theme file
 * (src/theme_builtin.c), and the program name used for messages and the
 * theme path.
 */
#ifndef LIBRARIAN_THEME_SLOTS_H
#define LIBRARIAN_THEME_SLOTS_H

#define THEME_PROGRAM "librarian"

typedef enum {
    THEME_RESET = 0,
    /* [brand]: the banner and the prompt */
    THEME_BRAND_GLYPH,
    THEME_BRAND_NAME,
    THEME_BRAND_TAGLINE,
    THEME_BRAND_FACTS,
    /* [status]: command output and progress */
    THEME_SUCCESS,  /* a finished action, a confident answer */
    THEME_PROGRESS, /* the ingestion status line, work in flight */
    THEME_ERROR,    /* a failed action */
    THEME_NOTE,     /* secondary detail: paths, counts, sources */
    THEME_INFO,     /* labels and highlighted values */
    THEME_HEADING,  /* section titles in help and listings */
    /* [menu]: the completion menu */
    THEME_SELECTED, /* the highlighted entry */
    THEME_MATCH,    /* an unselected entry */
    THEME_DETAIL,   /* a directory or secondary column */
    THEME_HINT,     /* the key hints below the menu */
    THEME_STYLE_COUNT
} ThemeStyle;

/* Where each style is written in a theme file. RESET is not configurable: it
 * is the sequence that ends every other style. */
typedef struct {
    const char *section;
    const char *key;
} StyleName;

/* A built-in palette, written in the theme-file format. */
typedef struct {
    const char *name;
    const char *text;
} ThemeBuiltin;

extern const StyleName theme_names[THEME_STYLE_COUNT];

/* NULL-terminated; the first entry is "default". */
extern const ThemeBuiltin theme_builtins[];

#endif /* LIBRARIAN_THEME_SLOTS_H */
