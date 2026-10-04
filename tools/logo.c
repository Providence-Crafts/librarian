/*
 * logo.c - write docs/logo.svg from the banner art.
 *
 * The README logo is the same bitmap the banner draws, one 1x1 rect per
 * opaque pixel, so the two cannot drift apart. Run through `make logo`;
 * the output goes to stdout.
 */
#include "../src/brand_art.h"

#include <stdio.h>
#include <string.h>

static const char g_index[] = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";

int main(void)
{
    unsigned x;
    unsigned y;

    printf("<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 %d %d\" width=\"192\" "
           "height=\"192\" shape-rendering=\"crispEdges\">\n",
           BRAND_ART_W, BRAND_ART_H);
    printf("<title>librarian</title>\n");
    for (y = 0u; y < BRAND_ART_H; y++) {
        for (x = 0u; x < BRAND_ART_W; x++) {
            const char *at = strchr(g_index, g_art[y][x]);
            const unsigned char *c;

            if (g_art[y][x] == '.' || at == NULL) {
                continue;
            }
            c = g_palette[at - g_index];
            printf("<rect x=\"%u\" y=\"%u\" width=\"1\" height=\"1\" fill=\"#%02x%02x%02x\"/>\n", x,
                   y, c[0], c[1], c[2]);
        }
    }
    printf("</svg>\n");
    return 0;
}
