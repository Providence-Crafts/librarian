#ifndef LIBRARIAN_UI_H
#define LIBRARIAN_UI_H

#include <stdbool.h>
#include <stddef.h>

/* Style tokens for ui_printf. Each is an in-band marker (SO plus a letter,
 * concatenated so the letter is never read as a hex digit) that ui_printf
 * replaces with the theme's SGR sequence for that slot, or with nothing when
 * colour is off. Outside ui_printf they must not be printed. */
#define STYLE_RESET                                                                                \
    "\x0e"                                                                                         \
    "0"
#define STYLE_HEADING                                                                              \
    "\x0e"                                                                                         \
    "h"
#define STYLE_SUCCESS                                                                              \
    "\x0e"                                                                                         \
    "s"
#define STYLE_PROGRESS                                                                             \
    "\x0e"                                                                                         \
    "p"
#define STYLE_ERROR                                                                                \
    "\x0e"                                                                                         \
    "e"
#define STYLE_NOTE                                                                                 \
    "\x0e"                                                                                         \
    "n"
#define STYLE_INFO                                                                                 \
    "\x0e"                                                                                         \
    "i"

#include "db.h"

/* The start-up banner, sized to the terminal (src/brand.c). */
void ui_banner(void);
/* The interactive prompt, themed (src/brand.c). */
void ui_prompt(void);
void ui_status(const char *status);
void ui_clear_status(void);
void ui_ingest_progress(size_t current, size_t total, const char *file_path);
void ui_ingest_progress_chunk(size_t current_doc, size_t total_docs, const char *file_path,
                              int current_chunk, int total_chunks);
void ui_confidence_badge(float confidence, bool is_refusal);
void ui_similarity_badge(float similarity, float threshold);
void ui_references(const search_result_t *results, int count, float min_threshold);
void ui_list_documents(const doc_info_t *docs, int count, const char *search_pattern);
void ui_list_chunks(int64_t doc_id, const char *doc_path, const chunk_info_t *chunks, int count,
                    int limit);
bool ui_confirm(const char *prompt);

#if defined(__GNUC__) || defined(__clang__)
#define ATTR_PRINTF(fmt_idx, arg_idx) __attribute__((format(printf, fmt_idx, arg_idx)))
#else
#define ATTR_PRINTF(fmt_idx, arg_idx)
#endif

/* printf to stdout, expanding STYLE_* tokens in FMT through the theme. */
int ui_printf(const char *fmt, ...) ATTR_PRINTF(1, 2);

void ui_info(const char *fmt, ...) ATTR_PRINTF(1, 2);
void ui_success(const char *fmt, ...) ATTR_PRINTF(1, 2);
void ui_warn(const char *fmt, ...) ATTR_PRINTF(1, 2);
void ui_error(const char *fmt, ...) ATTR_PRINTF(1, 2);

#endif /* LIBRARIAN_UI_H */
