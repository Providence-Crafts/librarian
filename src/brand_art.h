/*
 * brand_art.h - the librarian bookshelf, as a palette-indexed bitmap.
 *
 * Shared by src/brand.c, which draws it in half blocks, and tools/logo.c,
 * which writes docs/logo.svg from it, so the banner and the README logo
 * cannot drift apart. Include it from exactly one translation unit per
 * program: the data is static.
 */
#ifndef LIBRARIAN_BRAND_ART_H
#define LIBRARIAN_BRAND_ART_H

/* clang-format off */
/* 24 x 24 pixels, 62 colours. '.' is transparent; the other characters index
 * g_palette in the order 0-9, A-Z, a-z. */
#define BRAND_ART_W 24
#define BRAND_ART_H 24

static const unsigned char g_palette[62][3] = {
    {169, 137, 83},
    {35, 27, 14},
    {37, 73, 117},
    {130, 21, 16},
    {117, 114, 14},
    {104, 121, 116},
    {60, 72, 60},
    {131, 103, 60},
    {20, 89, 69},
    {100, 56, 12},
    {100, 98, 65},
    {95, 16, 11},
    {56, 69, 13},
    {30, 57, 92},
    {28, 36, 45},
    {81, 90, 13},
    {69, 31, 18},
    {114, 72, 46},
    {77, 88, 83},
    {149, 119, 72},
    {93, 74, 67},
    {25, 66, 52},
    {124, 110, 46},
    {52, 44, 54},
    {74, 99, 40},
    {54, 89, 26},
    {113, 43, 24},
    {88, 68, 40},
    {63, 59, 71},
    {51, 42, 26},
    {89, 34, 12},
    {27, 90, 91},
    {161, 130, 78},
    {114, 106, 92},
    {105, 84, 47},
    {128, 126, 104},
    {36, 66, 62},
    {121, 94, 55},
    {94, 77, 45},
    {95, 85, 12},
    {74, 60, 13},
    {50, 63, 77},
    {180, 145, 90},
    {95, 94, 89},
    {49, 60, 40},
    {103, 119, 33},
    {130, 105, 105},
    {65, 52, 29},
    {118, 124, 82},
    {47, 29, 15},
    {16, 68, 67},
    {140, 110, 65},
    {110, 22, 13},
    {74, 24, 11},
    {51, 45, 17},
    {27, 52, 38},
    {91, 53, 32},
    {72, 90, 56},
    {65, 74, 27},
    {38, 45, 54},
    {117, 106, 74},
    {109, 90, 55},
};

static const char *const g_art[BRAND_ART_H] = {
    "..........0ggg..........",
    "........gWJ0g0gg........",
    "......bp0g00WW00gg......",
    "....gggWpW00gWbpWggg....",
    ".0gJ7J0gg0JpJW00p7W0ggg.",
    "b7J0ggWpJ000p70gg0770WW7",
    "7zbbWW007bJW0gWpW0gWpYRY",
    "b1lcbpJ00gWJJW0gW7p7RfEc",
    "brsGTR77pW0g077W7Yls1DLc",
    "JqU3rnTcbb7J0gpRl1ndGDoA",
    "73U3GTedsIzbbclnn1wAUDoc",
    "bQG3SxjZu2xbR11BG1FdeDac",
    "pp7QSOF4Q2abRnrqNiCFeKcY",
    "7RbJJze492VAuBrqNwlYzRTc",
    "7qnlzp7M92VmHBnBRzYRT11R",
    "JkiDwFYpJhVAHrRbYRT1sCLc",
    "p3a2vFGTc777cYYlsCrnCwIz",
    "bqL26FqGsslbcl1EiCBBeCoc",
    "pHR2fNqqFCnzRrnDiTrBeC6Y",
    ".JWXvwG3dO8zHKLDxTnUczY.",
    "....WMcqdO5yHBtDiwcz....",
    "......ppYP8yHBlSAz......",
    "........JpIyRuz7........",
    "..........ppYz..........",
};
/* clang-format on */

#endif /* LIBRARIAN_BRAND_ART_H */
