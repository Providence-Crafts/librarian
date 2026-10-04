#include "ui.h"

#include "brand.h"
#include "plat.h"
#include "theme.h"
#include "version.h"

#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <io.h>
#include <windows.h>
#ifndef STDIN_FILENO
#define STDIN_FILENO 0
#define STDOUT_FILENO 1
#define STDERR_FILENO 2
#endif
#define isatty _isatty
#define fileno _fileno
#else
#include <sys/ioctl.h>
#include <unistd.h>
#endif

/* The SGR for a STYLE_* token letter, "" for an unknown one. */
static const char *token_sgr(char letter)
{
    switch (letter) {
    case '0':
        return theme_sgr(THEME_RESET);
    case 'h':
        return theme_sgr(THEME_HEADING);
    case 's':
        return theme_sgr(THEME_SUCCESS);
    case 'p':
        return theme_sgr(THEME_PROGRESS);
    case 'e':
        return theme_sgr(THEME_ERROR);
    case 'n':
        return theme_sgr(THEME_NOTE);
    case 'i':
        return theme_sgr(THEME_INFO);
    default:
        return "";
    }
}

int ui_printf(const char *fmt, ...)
{
    char stack[1024];
    char *buf = stack;
    size_t need = 1u;
    size_t n = 0u;
    const char *p;
    va_list ap;
    int rc;

    for (p = fmt; *p != '\0'; p++) {
        if (*p == '\x0e' && p[1] != '\0') {
            need += strlen(token_sgr(*++p));
        } else {
            need++;
        }
    }
    if (need > sizeof(stack)) {
        buf = malloc(need);
        if (!buf) {
            return -1;
        }
    }
    for (p = fmt; *p != '\0'; p++) {
        if (*p == '\x0e' && p[1] != '\0') {
            const char *sgr = token_sgr(*++p);
            size_t len = strlen(sgr);

            memcpy(buf + n, sgr, len);
            n += len;
        } else {
            buf[n++] = *p;
        }
    }
    buf[n] = '\0';

    va_start(ap, fmt);
    rc = vprintf(buf, ap); /* NOLINT(clang-diagnostic-format-nonliteral) */
    va_end(ap);
    if (buf != stack) {
        free(buf);
    }
    return rc;
}

void ui_banner(void)
{
    unsigned cols = 80u;
    unsigned rows = 0u;

    (void)plat_term_size(stdout, &cols, &rows);
    brand_banner(stdout, cols, LIBRARIAN_VERSION);
}

void ui_prompt(void)
{
    char prompt[128];

    brand_prompt(prompt, sizeof(prompt));
    fputs(prompt, stdout);
    fflush(stdout);
}

void ui_status(const char *status)
{
    ui_printf("\r\x1b[K" STYLE_PROGRESS "%s" STYLE_RESET, status);
    fflush(stdout);
}

void ui_clear_status(void)
{
    printf("\r\x1b[K");
    fflush(stdout);
}

static int get_terminal_width(void)
{
    int term_w = 80;
#ifdef _WIN32
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    if (GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), &csbi)) {
        int w = csbi.srWindow.Right - csbi.srWindow.Left + 1;
        if (w > 20) {
            term_w = w;
        }
    }
#else
    struct winsize ws;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0 && ws.ws_col > 20) {
        term_w = ws.ws_col;
    }
#endif
    return term_w;
}

void ui_ingest_progress(size_t current, size_t total, const char *file_path)
{
    if (!isatty(STDOUT_FILENO)) {
        return;
    }
    if (total == 0) {
        return;
    }

    int pct = (int)((current * 100) / total);
    int term_w = get_terminal_width();

    /* Compact home path if applicable */
    const char *disp_path = file_path;
    char home_buf[1024];
    const char *home = getenv("HOME");
    if (home && strncmp(file_path, home, strlen(home)) == 0) {
        snprintf(home_buf, sizeof(home_buf), "~%s", file_path + strlen(home));
        disp_path = home_buf;
    }

    /* Reserve columns for prefix: "📦 Ingesting [1977/1977] (100%) • " */
    char prefix[128];
    snprintf(prefix, sizeof(prefix), "📦 Ingesting [%zu/%zu] (%d%%) • ", current, total, pct);
    int pcol = (int)strlen(prefix);

    int avail = term_w - pcol - 1;
    if (avail < 10) {
        avail = 10;
    }

    int path_len = (int)strlen(disp_path);
    char path_buf[512];
    if (path_len > avail && avail > 4) {
        snprintf(path_buf, sizeof(path_buf), "...%.*s", (int)(sizeof(path_buf) - 4),
                 disp_path + (path_len - (avail - 3)));
    } else {
        snprintf(path_buf, sizeof(path_buf), "%.*s", (int)(sizeof(path_buf) - 1), disp_path);
    }

    ui_printf("\r\x1b[K" STYLE_HEADING "📦 Ingesting [" STYLE_SUCCESS "%zu/%zu" STYLE_HEADING
              "] " STYLE_PROGRESS "(%3d%%)" STYLE_RESET " " STYLE_INFO "•" STYLE_RESET
              " " STYLE_NOTE "%s" STYLE_RESET,
              current, total, pct, path_buf);
    fflush(stdout);
}

void ui_ingest_progress_chunk(size_t current, size_t total, const char *file_path,
                              int current_chunk, int total_chunks)
{
    if (total == 0 || !file_path) {
        return;
    }

    int pct = (int)((current * 100) / total);
    if (pct > 100) {
        pct = 100;
    }

    int chunk_pct = (total_chunks > 0) ? (current_chunk * 100) / total_chunks : 100;
    if (chunk_pct > 100) {
        chunk_pct = 100;
    }

    int term_w = get_terminal_width();
    char home_buf[512];
    const char *disp_path = file_path;
    const char *home = getenv("HOME");
    if (home && strncmp(file_path, home, strlen(home)) == 0) {
        snprintf(home_buf, sizeof(home_buf), "~%s", file_path + strlen(home));
        disp_path = home_buf;
    }

    char chunk_tag[64];
    snprintf(chunk_tag, sizeof(chunk_tag), " [chunk %d/%d - %d%%]", current_chunk, total_chunks,
             chunk_pct);

    char prefix[128];
    snprintf(prefix, sizeof(prefix), "📦 Ingesting [%zu/%zu] (%d%%) • ", current, total, pct);
    int pcol = (int)strlen(prefix) + (int)strlen(chunk_tag);

    int avail = term_w - pcol - 1;
    if (avail < 10) {
        avail = 10;
    }

    int path_len = (int)strlen(disp_path);
    char path_buf[512];
    if (path_len > avail && avail > 4) {
        snprintf(path_buf, sizeof(path_buf), "...%.*s", (int)(sizeof(path_buf) - 4),
                 disp_path + (path_len - (avail - 3)));
    } else {
        snprintf(path_buf, sizeof(path_buf), "%.*s", (int)(sizeof(path_buf) - 1), disp_path);
    }

    ui_printf("\r\x1b[K" STYLE_HEADING "📦 Ingesting [" STYLE_SUCCESS "%zu/%zu" STYLE_HEADING
              "] " STYLE_PROGRESS "(%3d%%)" STYLE_RESET " " STYLE_INFO "•" STYLE_RESET
              " " STYLE_NOTE "%s" STYLE_PROGRESS "%s" STYLE_RESET,
              current, total, pct, path_buf, chunk_tag);
    fflush(stdout);
}

void ui_confidence_badge(float confidence, bool is_refusal)
{
    int pct = (int)(confidence * 100.0f);
    if (pct < 0) {
        pct = 0;
    }
    if (pct > 100) {
        pct = 100;
    }

    if (is_refusal) {
        ui_printf(STYLE_PROGRESS "✦ [Refusal Confidence: %d%%]" STYLE_RESET "\n", pct);
    } else {
        ui_printf(STYLE_SUCCESS "✦ [Confidence: %d%%]" STYLE_RESET "\n", pct);
    }
}

void ui_similarity_badge(float similarity, float threshold)
{
    ui_printf(STYLE_HEADING "◈ [Similarity: %.3f / min: %.3f]" STYLE_RESET "\n", (double)similarity,
              (double)threshold);
}

static void format_snippet(const char *content, char *out, size_t out_sz)
{
    if (!content || !out || out_sz == 0) {
        if (out && out_sz > 0) {
            out[0] = '\0';
        }
        return;
    }

    const char *p = content;
    while (*p && isspace((unsigned char)*p)) {
        p++;
    }

    size_t written = 0;
    bool in_space = false;
    size_t max_len = (out_sz > 84) ? 84 : out_sz - 4;

    while (*p && written < max_len) {
        if (isspace((unsigned char)*p)) {
            if (!in_space && written > 0) {
                out[written++] = ' ';
                in_space = true;
            }
        } else {
            out[written++] = *p;
            in_space = false;
        }
        p++;
    }

    while (written > 0 && isspace((unsigned char)out[written - 1])) {
        written--;
    }

    if (*p && written + 3 < out_sz) {
        out[written++] = '.';
        out[written++] = '.';
        out[written++] = '.';
    }
    out[written] = '\0';
}

void ui_references(const search_result_t *results, int count, float min_threshold)
{
    if (!results || count <= 0) {
        return;
    }

    int valid_count = 0;
    for (int i = 0; i < count; i++) {
        if (results[i].similarity >= min_threshold) {
            valid_count++;
        }
    }
    if (valid_count == 0) {
        return;
    }

    ui_printf(STYLE_HEADING "📑 References:" STYLE_RESET "\n");

    const char *home = getenv("HOME");
    size_t home_len = home ? strlen(home) : 0;

    int ref_idx = 1;
    for (int i = 0; i < count; i++) {
        const search_result_t *r = &results[i];
        if (r->similarity < min_threshold) {
            continue;
        }

        /* Compact home path */
        char path_buf[1024];
        const char *raw_path = r->doc_path;
        if (home_len > 0 && strncmp(r->doc_path, home, home_len) == 0) {
            snprintf(path_buf, sizeof(path_buf), "~%s", r->doc_path + home_len);
            raw_path = path_buf;
        }

        /* Clean consecutive slashes */
        char clean_path[1024];
        size_t c_idx = 0;
        bool last_slash = false;
        for (size_t s = 0; raw_path[s] && c_idx + 1 < sizeof(clean_path); s++) {
            if (raw_path[s] == '/') {
                if (!last_slash) {
                    clean_path[c_idx++] = '/';
                    last_slash = true;
                }
            } else {
                clean_path[c_idx++] = raw_path[s];
                last_slash = false;
            }
        }
        clean_path[c_idx] = '\0';
        const char *disp_path = clean_path;

        /* Extract filename */
        const char *slash = strrchr(disp_path, '/');
        const char *fname = slash ? slash + 1 : disp_path;

        ui_printf("  " STYLE_INFO "[%d]" STYLE_RESET " " STYLE_SUCCESS "%.1f%%" STYLE_RESET
                  " " STYLE_INFO "•" STYLE_RESET " " STYLE_HEADING "%s" STYLE_RESET
                  " " STYLE_PROGRESS "[chunk #%d]" STYLE_RESET "\n",
                  ref_idx, (double)(r->similarity * 100.0f), fname, r->chunk_idx);

        ui_printf("      " STYLE_NOTE "%s" STYLE_RESET "\n", disp_path);

        if (r->content && r->content[0] != '\0') {
            char snippet[128];
            format_snippet(r->content, snippet, sizeof(snippet));
            if (snippet[0] != '\0') {
                ui_printf("      " STYLE_NOTE "\"%s\"" STYLE_RESET "\n", snippet);
            }
        }
        printf("\n");
        ref_idx++;
    }
}

void ui_list_documents(const doc_info_t *docs, int count, const char *search_pattern)
{
    if (!docs || count == 0) {
        if (search_pattern && search_pattern[0] != '\0') {
            ui_printf(STYLE_PROGRESS "No documents found matching '%s'." STYLE_RESET "\n\n",
                      search_pattern);
        } else {
            ui_printf(STYLE_PROGRESS
                      "No documents currently indexed in the knowledge base." STYLE_RESET "\n\n");
        }
        return;
    }

    if (search_pattern && search_pattern[0] != '\0') {
        ui_printf(STYLE_HEADING "🗂 Indexed Documents (%d matching '%s'):" STYLE_RESET "\n", count,
                  search_pattern);
    } else {
        ui_printf(STYLE_HEADING "🗂 Indexed Documents (%d total):" STYLE_RESET "\n", count);
    }

    const char *home = getenv("HOME");
    size_t home_len = home ? strlen(home) : 0;
    int page_size = 15;
    int total_pages = (count + page_size - 1) / page_size;
    bool interactive = isatty(fileno(stdin)) && isatty(fileno(stdout)) && (total_pages > 1);
    int current_page = 0;

    while (1) {
        int start = current_page * page_size;
        int end = start + page_size;
        if (end > count) {
            end = count;
        }

        if (interactive) {
            if (search_pattern && search_pattern[0] != '\0') {
                ui_printf(STYLE_HEADING
                          "🗂 Indexed Documents [Page %d/%d] (%d matching '%s'):" STYLE_RESET "\n",
                          current_page + 1, total_pages, count, search_pattern);
            } else {
                ui_printf(STYLE_HEADING "🗂 Indexed Documents [Page %d/%d] (%d total):" STYLE_RESET
                                        "\n",
                          current_page + 1, total_pages, count);
            }
        } else if (current_page == 0) {
            if (search_pattern && search_pattern[0] != '\0') {
                ui_printf(STYLE_HEADING "🗂 Indexed Documents (%d matching '%s'):" STYLE_RESET "\n",
                          count, search_pattern);
            } else {
                ui_printf(STYLE_HEADING "🗂 Indexed Documents (%d total):" STYLE_RESET "\n", count);
            }
        }

        for (int i = start; i < end; i++) {
            const doc_info_t *d = &docs[i];

            char path_buf[1024];
            const char *raw_path = d->path;
            if (home_len > 0 && strncmp(d->path, home, home_len) == 0) {
                snprintf(path_buf, sizeof(path_buf), "~%s", d->path + home_len);
                raw_path = path_buf;
            }

            char clean_path[1024];
            size_t c_idx = 0;
            bool last_slash = false;
            for (size_t s = 0; raw_path[s] && c_idx + 1 < sizeof(clean_path); s++) {
                if (raw_path[s] == '/') {
                    if (!last_slash) {
                        clean_path[c_idx++] = '/';
                        last_slash = true;
                    }
                } else {
                    clean_path[c_idx++] = raw_path[s];
                    last_slash = false;
                }
            }
            clean_path[c_idx] = '\0';
            const char *disp_path = clean_path;

            const char *slash = strrchr(disp_path, '/');
            const char *fname = slash ? slash + 1 : disp_path;

            ui_printf("  " STYLE_INFO "#%-4lld" STYLE_RESET " " STYLE_PROGRESS
                      "[%3d chunks]" STYLE_RESET " " STYLE_HEADING "%s" STYLE_RESET "\n",
                      (long long)d->id, d->chunk_count, fname);
            ui_printf("         " STYLE_NOTE "%s" STYLE_RESET "\n", disp_path);
        }

        if (!interactive) {
            break;
        }

        ui_printf("\n" STYLE_INFO
                  "── [Page %d/%d] (Enter/n: next, p: prev, q: quit, 1-%d: page): " STYLE_RESET,
                  current_page + 1, total_pages, total_pages);
        fflush(stdout);

        char line[64];
        if (!fgets(line, sizeof(line), stdin)) {
            printf("\n");
            break;
        }

        const char *t = line;
        while (*t && isspace((unsigned char)*t)) {
            t++;
        }
        if (*t == 'q' || *t == 'Q') {
            printf("\n");
            break;
        }
        if (*t == 'p' || *t == 'P') {
            if (current_page > 0) {
                current_page--;
            }
        } else if (*t == 'n' || *t == 'N' || *t == '\0') {
            if (current_page + 1 < total_pages) {
                current_page++;
            } else {
                ui_printf(STYLE_NOTE "Reached end of list." STYLE_RESET "\n\n");
                break;
            }
        } else if (isdigit((unsigned char)*t)) {
            int pnum = (int)strtol(t, NULL, 10);
            if (pnum >= 1 && pnum <= total_pages) {
                current_page = pnum - 1;
            }
        }
    }
    ui_printf(
        "\n" STYLE_NOTE
        "💡 Tip: Inspect chunks with `/chunks <ID>` or `./bin/librarian chunks <ID>`" STYLE_RESET
        "\n\n");
}

void ui_list_chunks(int64_t doc_id, const char *doc_path, const chunk_info_t *chunks, int count,
                    int limit)
{
    if (!chunks || count == 0) {
        ui_printf(STYLE_PROGRESS "No chunks found for document #%lld." STYLE_RESET "\n\n",
                  (long long)doc_id);
        return;
    }

    const char *home = getenv("HOME");
    size_t home_len = home ? strlen(home) : 0;
    char path_buf[1024];
    const char *raw_path = doc_path ? doc_path : "";
    if (home_len > 0 && doc_path && strncmp(doc_path, home, home_len) == 0) {
        snprintf(path_buf, sizeof(path_buf), "~%s", doc_path + home_len);
        raw_path = path_buf;
    }

    char clean_path[1024];
    size_t c_idx = 0;
    bool last_slash = false;
    for (size_t s = 0; raw_path[s] && c_idx + 1 < sizeof(clean_path); s++) {
        if (raw_path[s] == '/') {
            if (!last_slash) {
                clean_path[c_idx++] = '/';
                last_slash = true;
            }
        } else {
            clean_path[c_idx++] = raw_path[s];
            last_slash = false;
        }
    }
    clean_path[c_idx] = '\0';
    const char *disp_path = clean_path;

    const char *slash = strrchr(disp_path, '/');
    const char *fname = slash ? slash + 1 : disp_path;

    int show_count = count;
    if (limit > 0 && show_count > limit) {
        show_count = limit;
    }

    int page_size = 15;
    int total_pages = (show_count + page_size - 1) / page_size;
    bool interactive = isatty(fileno(stdin)) && isatty(fileno(stdout)) && (total_pages > 1);
    int current_page = 0;

    while (1) {
        int start = current_page * page_size;
        int end = start + page_size;
        if (end > show_count) {
            end = show_count;
        }

        if (interactive) {
            ui_printf(STYLE_HEADING "📄 Document #%lld: %s" STYLE_RESET " " STYLE_PROGRESS
                                    "[Page %d/%d] (%d chunks total)" STYLE_RESET "\n",
                      (long long)doc_id, fname, current_page + 1, total_pages, count);
        } else if (current_page == 0) {
            ui_printf(STYLE_HEADING "📄 Document #%lld: %s" STYLE_RESET " " STYLE_PROGRESS
                                    "(%d chunks total)" STYLE_RESET "\n",
                      (long long)doc_id, fname, count);
            ui_printf("   " STYLE_NOTE "%s" STYLE_RESET "\n\n", disp_path);
        }

        for (int i = start; i < end; i++) {
            const chunk_info_t *c = &chunks[i];
            char snippet[128];
            format_snippet(c->content, snippet, sizeof(snippet));

            ui_printf("  " STYLE_INFO "#%-3d" STYLE_RESET " " STYLE_PROGRESS "(%3dw)" STYLE_RESET
                      " " STYLE_NOTE "\"%s\"" STYLE_RESET "\n",
                      c->chunk_idx, c->word_count, snippet);
        }

        if (!interactive) {
            break;
        }

        ui_printf("\n" STYLE_INFO
                  "── [Page %d/%d] (Enter/n: next, p: prev, q: quit, 1-%d: page): " STYLE_RESET,
                  current_page + 1, total_pages, total_pages);
        fflush(stdout);

        char line[64];
        if (!fgets(line, sizeof(line), stdin)) {
            printf("\n");
            break;
        }

        const char *t = line;
        while (*t && isspace((unsigned char)*t)) {
            t++;
        }
        if (*t == 'q' || *t == 'Q') {
            printf("\n");
            break;
        }
        if (*t == 'p' || *t == 'P') {
            if (current_page > 0) {
                current_page--;
            }
        } else if (*t == 'n' || *t == 'N' || *t == '\0') {
            if (current_page + 1 < total_pages) {
                current_page++;
            } else {
                ui_printf(STYLE_NOTE "Reached end of chunks." STYLE_RESET "\n\n");
                break;
            }
        } else if (isdigit((unsigned char)*t)) {
            int pnum = (int)strtol(t, NULL, 10);
            if (pnum >= 1 && pnum <= total_pages) {
                current_page = pnum - 1;
            }
        }
    }

    if (!interactive && show_count < count) {
        ui_printf("\n" STYLE_NOTE
                  "  ... and %d more chunks (specify limit, e.g. `/chunks %lld %d`)" STYLE_RESET
                  "\n",
                  count - show_count, (long long)doc_id, count);
    }
    printf("\n");
}

void ui_info(const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    ui_printf(STYLE_INFO "ℹ " STYLE_RESET);
    vprintf(fmt, args);
    printf("\n");
    va_end(args);
}

void ui_success(const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    ui_printf(STYLE_SUCCESS "✔ " STYLE_RESET);
    vprintf(fmt, args);
    printf("\n");
    va_end(args);
}

void ui_warn(const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    ui_printf(STYLE_PROGRESS "⚠ " STYLE_RESET);
    vprintf(fmt, args);
    printf("\n");
    va_end(args);
}

void ui_error(const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    ui_printf(STYLE_ERROR "✘ " STYLE_RESET);
    vprintf(fmt, args);
    printf("\n");
    va_end(args);
}

bool ui_confirm(const char *prompt)
{
    ui_printf(STYLE_PROGRESS "⚠ %s " STYLE_RESET, prompt);
    fflush(stdout);

    char buf[64] = {0};
    if (!fgets(buf, sizeof(buf), stdin)) {
        return false;
    }

    /* Strip leading whitespace */
    const char *p = buf;
    while (*p && isspace((unsigned char)*p)) {
        p++;
    }
    return (*p == 'y' || *p == 'Y');
}
