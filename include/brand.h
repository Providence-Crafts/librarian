/*
 * brand.h - the start-up banner and the interactive prompt.
 *
 * librarian shares a design language with its sibling program redstone: an
 * isometric Minecraft block drawn in half-block characters on the left, a
 * three-line caption beside it, and a `glyph name ❯` prompt. The block is the
 * only part that differs per program; here it is a bookshelf.
 *
 * Both draw only through the theme (the [brand] section), so NO_COLOR, a dumb
 * terminal or a user's theme file are honoured. The art is raw 24-bit colour,
 * because it is an image and not a styled span of text; it is therefore shown
 * only where the terminal says it can render it.
 */
#ifndef LIBRARIAN_BRAND_H
#define LIBRARIAN_BRAND_H

#include <stddef.h>
#include <stdio.h>

/* Write the banner to OUT for a terminal COLS columns wide. The art appears
 * only with colour on, a UTF-8 locale, truecolor support and room for the
 * captions beside it; otherwise just the captions are written. */
void brand_banner(FILE *out, unsigned cols, const char *version);

/* Write the main prompt into BUF (SIZE bytes, always terminated). It contains
 * escape sequences and a multi-byte glyph, so its width is not its length. */
void brand_prompt(char *buf, size_t size);

#endif /* LIBRARIAN_BRAND_H */
