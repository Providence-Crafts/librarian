#ifndef LIBRARIAN_DOC_H
#define LIBRARIAN_DOC_H

#include <stdbool.h>
#include <stddef.h>

/* Checks if a file extension (without dot) is supported for text extraction */
bool doc_is_supported_extension(const char *ext);

/* Checks if a file extension (without dot) should be skipped (binaries, media, archives) */
bool doc_is_ignored_extension(const char *ext);

/* Checks if a file on disk is binary by inspecting initial bytes for null characters */
bool doc_is_binary_file(const char *path);

/* Checks if a file on disk is a Git LFS pointer (un-pulled file) */
bool doc_is_git_lfs_pointer(const char *path);

/* Escapes a path safely for POSIX shell command execution. Caller must free() result. */
char *doc_shell_escape(const char *str);

/* Pure C text extractors for structured formats */
char *doc_extract_html_text(const char *html_str);
char *doc_extract_docx_text(const char *path);
char *doc_extract_odt_text(const char *path);
char *doc_extract_epub_text(const char *path);

/* Extracts clean, sanitized UTF-8 text from a document (.pdf, .epub, .md, .txt, etc.).
   Returns dynamically allocated string (caller must free), or NULL if unsupported/failed. */
char *doc_extract_text(const char *file_path);

/* Cleans, unquotes, unescapes, trims trailing/leading whitespace, and expands '~' home directory. */
void doc_clean_path(const char *in, char *out, size_t out_sz);

#endif /* LIBRARIAN_DOC_H */
