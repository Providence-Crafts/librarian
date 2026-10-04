#include "doc.h"

#include "embedder.h"
#include "logger.h"
#include "miniz.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#if defined(_WIN32)
#define popen _popen
#define pclose _pclose
#endif

static const char *const SUPPORTED_EXTS[] = {
    /* Rich prose documents & eBooks */
    "pdf", "epub", "mobi", "odt", "docx", "doc", "rtf",
    /* Plain text & Markdown variants */
    "txt", "text", "md", "markdown", "mdown", "mkdn",
    /* Technical & prose documentation formats */
    "rst", "org", "adoc", "asciidoc", "tex", "html", "htm"};

static const char *const IGNORED_EXTS[] = {
    /* Archives */
    "zip", "tar", "gz", "bz2", "xz", "7z", "rar", "zst", "tgz",
    /* Binaries, object files, libraries */
    "pyc", "pyo", "pyd", "o", "a", "so", "dylib", "dll", "exe", "bin", "iso", "class",
    /* Media */
    "png", "jpg", "jpeg", "gif", "bmp", "webp", "tiff", "svg", "ico", "mp3", "wav", "flac", "ogg",
    "m4a", "mp4", "mkv", "avi", "mov", "webm",
    /* Spreadsheets, binary databases */
    "db", "sqlite", "sqlite3", "parquet", "arrow", "feather", "xlsm", "xlsx", "xls", "ods",
    /* Miscellaneous binaries */
    "lock", "sentinal", "idx", "pack"};

bool doc_is_supported_extension(const char *ext)
{
    if (!ext) {
        return false;
    }
    size_t count = sizeof(SUPPORTED_EXTS) / sizeof(SUPPORTED_EXTS[0]);
    for (size_t i = 0; i < count; i++) {
        if (strcasecmp(ext, SUPPORTED_EXTS[i]) == 0) {
            return true;
        }
    }
    return false;
}

bool doc_is_ignored_extension(const char *ext)
{
    if (!ext) {
        return false;
    }
    size_t count = sizeof(IGNORED_EXTS) / sizeof(IGNORED_EXTS[0]);
    for (size_t i = 0; i < count; i++) {
        if (strcasecmp(ext, IGNORED_EXTS[i]) == 0) {
            return true;
        }
    }
    return false;
}

bool doc_is_binary_file(const char *path)
{
    if (!path) {
        return true;
    }
    FILE *fp = fopen(path, "rb");
    if (!fp) {
        return true;
    }
    unsigned char buf[1024];
    size_t n = fread(buf, 1, sizeof(buf), fp);
    fclose(fp);

    for (size_t i = 0; i < n; i++) {
        if (buf[i] == 0) {
            return true;
        }
    }
    return false;
}

char *doc_shell_escape(const char *str)
{
    if (!str) {
        return NULL;
    }
    size_t len = strlen(str);
    char *escaped = malloc(len * 4 + 3);
    if (!escaped) {
        return NULL;
    }

    char *p = escaped;
    *p++ = '\'';
    for (size_t i = 0; i < len; i++) {
        if (str[i] == '\'') {
            *p++ = '\'';
            *p++ = '\\';
            *p++ = '\'';
            *p++ = '\'';
        } else {
            *p++ = str[i];
        }
    }
    *p++ = '\'';
    *p = '\0';
    return escaped;
}

static char *read_pipe_output(FILE *fp)
{
    if (!fp) {
        return NULL;
    }

    size_t cap = 64 * 1024;
    size_t len = 0;
    char *buf = malloc(cap);
    if (!buf) {
        return NULL;
    }

    char chunk[16384];
    size_t n;
    while ((n = fread(chunk, 1, sizeof(chunk), fp)) > 0) {
        if (len + n + 1 > cap) {
            size_t ncap = cap * 2;
            if (ncap < len + n + 1024) {
                ncap = len + n + 1024;
            }
            char *grown = realloc(buf, ncap);
            if (!grown) {
                free(buf);
                return NULL;
            }
            buf = grown;
            cap = ncap;
        }
        memcpy(buf + len, chunk, n);
        len += n;
    }
    buf[len] = '\0';
    return buf;
}

static char *read_entire_plain_file(const char *path)
{
    FILE *fp = fopen(path, "rb");
    if (!fp) {
        return NULL;
    }

    if (fseek(fp, 0, SEEK_END) != 0) {
        fclose(fp);
        return NULL;
    }
    long sz = ftell(fp);
    if (sz < 0) {
        fclose(fp);
        return NULL;
    }
    if (fseek(fp, 0, SEEK_SET) != 0) {
        fclose(fp);
        return NULL;
    }

    char *buf = malloc((size_t)sz + 1);
    if (!buf) {
        fclose(fp);
        return NULL;
    }

    size_t read_bytes = fread(buf, 1, (size_t)sz, fp);
    buf[read_bytes] = '\0';
    fclose(fp);
    return buf;
}

bool doc_is_git_lfs_pointer(const char *path)
{
    if (!path) {
        return false;
    }
    FILE *fp = fopen(path, "rb");
    if (!fp) {
        return false;
    }
    char head[128];
    size_t n = fread(head, 1, sizeof(head) - 1, fp);
    fclose(fp);
    if (n >= 23) {
        head[n] = '\0';
        if (strncmp(head, "version https://git-lfs", 23) == 0) {
            return true;
        }
    }
    return false;
}

static const char *find_case_insensitive(const char *haystack, const char *needle)
{
    if (!haystack || !needle) {
        return NULL;
    }
    size_t nlen = strlen(needle);
    if (nlen == 0) {
        return haystack;
    }
    for (const char *h = haystack; *h; h++) {
        if (strncasecmp(h, needle, nlen) == 0) {
            return h;
        }
    }
    return NULL;
}

char *doc_extract_html_text(const char *html_str)
{
    if (!html_str) {
        return NULL;
    }
    size_t len = strlen(html_str);
    char *out = malloc(len + 1);
    if (!out) {
        return NULL;
    }
    size_t o = 0;
    const char *p = html_str;

    while (*p) {
        if (*p == '<') {
            if (strncasecmp(p, "<script", 7) == 0) {
                const char *end = find_case_insensitive(p, "</script>");
                p = end ? end + 9 : p + strlen(p);
                continue;
            }
            if (strncasecmp(p, "<style", 6) == 0) {
                const char *end = find_case_insensitive(p, "</style>");
                p = end ? end + 8 : p + strlen(p);
                continue;
            }
            if (strncmp(p, "<!--", 4) == 0) {
                const char *end = strstr(p, "-->");
                p = end ? end + 3 : p + strlen(p);
                continue;
            }
            /* Insert newline on block tags */
            if (strncasecmp(p, "<p", 2) == 0 || strncasecmp(p, "<br", 3) == 0 ||
                strncasecmp(p, "<div", 4) == 0 || strncasecmp(p, "<h1", 3) == 0 ||
                strncasecmp(p, "<h2", 3) == 0 || strncasecmp(p, "<h3", 3) == 0 ||
                strncasecmp(p, "<h4", 3) == 0 || strncasecmp(p, "<h5", 3) == 0 ||
                strncasecmp(p, "<h6", 3) == 0 || strncasecmp(p, "<li", 3) == 0 ||
                strncasecmp(p, "<tr", 3) == 0 || strncasecmp(p, "</p>", 4) == 0 ||
                strncasecmp(p, "</div>", 6) == 0 || strncasecmp(p, "</li>", 5) == 0 ||
                strncasecmp(p, "<blockquote", 11) == 0) {
                if (o > 0 && out[o - 1] != '\n') {
                    out[o++] = '\n';
                }
            }
            const char *end = strchr(p, '>');
            p = end ? end + 1 : p + strlen(p);
            continue;
        }

        if (*p == '&') {
            if (strncmp(p, "&nbsp;", 6) == 0) {
                out[o++] = ' ';
                p += 6;
                continue;
            }
            if (strncmp(p, "&amp;", 5) == 0) {
                out[o++] = '&';
                p += 5;
                continue;
            }
            if (strncmp(p, "&lt;", 4) == 0) {
                out[o++] = '<';
                p += 4;
                continue;
            }
            if (strncmp(p, "&gt;", 4) == 0) {
                out[o++] = '>';
                p += 4;
                continue;
            }
            if (strncmp(p, "&quot;", 6) == 0) {
                out[o++] = '"';
                p += 6;
                continue;
            }
            if (strncmp(p, "&apos;", 6) == 0) {
                out[o++] = '\'';
                p += 6;
                continue;
            }
            if (strncmp(p, "&#39;", 5) == 0) {
                out[o++] = '\'';
                p += 5;
                continue;
            }
        }

        out[o++] = *p++;
    }
    out[o] = '\0';
    return out;
}

static char *doc_extract_xml_text(const char *xml, const char *open_tag, const char *close_tag,
                                  const char *p_break_tag)
{
    if (!xml || !open_tag || !close_tag) {
        return NULL;
    }
    size_t len = strlen(xml);
    char *out = malloc(len + 1);
    if (!out) {
        return NULL;
    }
    size_t o = 0;
    const char *p = xml;
    size_t open_len = strlen(open_tag);
    size_t close_len = strlen(close_tag);
    size_t p_len = p_break_tag ? strlen(p_break_tag) : 0;

    while (*p) {
        if (p_len > 0 && strncmp(p, p_break_tag, p_len) == 0) {
            if (o > 0 && out[o - 1] != '\n') {
                out[o++] = '\n';
            }
            p += p_len;
            continue;
        }

        if (strncmp(p, open_tag, open_len) == 0) {
            const char *gt = strchr(p, '>');
            if (!gt) {
                break;
            }
            p = gt + 1;
            const char *end = strstr(p, close_tag);
            if (!end) {
                while (*p && *p != '<') {
                    out[o++] = *p++;
                }
            } else {
                while (p < end) {
                    if (*p == '&') {
                        if (strncmp(p, "&amp;", 5) == 0) {
                            out[o++] = '&';
                            p += 5;
                            continue;
                        }
                        if (strncmp(p, "&lt;", 4) == 0) {
                            out[o++] = '<';
                            p += 4;
                            continue;
                        }
                        if (strncmp(p, "&gt;", 4) == 0) {
                            out[o++] = '>';
                            p += 4;
                            continue;
                        }
                        if (strncmp(p, "&quot;", 6) == 0) {
                            out[o++] = '"';
                            p += 6;
                            continue;
                        }
                        if (strncmp(p, "&apos;", 6) == 0) {
                            out[o++] = '\'';
                            p += 6;
                            continue;
                        }
                    }
                    out[o++] = *p++;
                }
                p = end + close_len;
            }
            continue;
        }
        p++;
    }
    out[o] = '\0';
    return out;
}

char *doc_extract_docx_text(const char *path)
{
    if (!path) {
        return NULL;
    }
    mz_zip_archive zip;
    memset(&zip, 0, sizeof(zip));
    if (!mz_zip_reader_init_file(&zip, path, 0)) {
        return NULL;
    }
    size_t uncomp_sz = 0;
    char *xml = (char *)mz_zip_reader_extract_file_to_heap(&zip, "word/document.xml", &uncomp_sz, 0);
    mz_zip_reader_end(&zip);
    if (!xml) {
        return NULL;
    }
    char *terminated_xml = malloc(uncomp_sz + 1);
    if (!terminated_xml) {
        mz_free(xml);
        return NULL;
    }
    memcpy(terminated_xml, xml, uncomp_sz);
    terminated_xml[uncomp_sz] = '\0';
    mz_free(xml);

    char *text = doc_extract_xml_text(terminated_xml, "<w:t", "</w:t>", "</w:p>");
    free(terminated_xml);
    return text;
}

char *doc_extract_odt_text(const char *path)
{
    if (!path) {
        return NULL;
    }
    mz_zip_archive zip;
    memset(&zip, 0, sizeof(zip));
    if (!mz_zip_reader_init_file(&zip, path, 0)) {
        return NULL;
    }
    size_t uncomp_sz = 0;
    char *xml = (char *)mz_zip_reader_extract_file_to_heap(&zip, "content.xml", &uncomp_sz, 0);
    mz_zip_reader_end(&zip);
    if (!xml) {
        return NULL;
    }
    char *terminated_xml = malloc(uncomp_sz + 1);
    if (!terminated_xml) {
        mz_free(xml);
        return NULL;
    }
    memcpy(terminated_xml, xml, uncomp_sz);
    terminated_xml[uncomp_sz] = '\0';
    mz_free(xml);

    char *text = doc_extract_xml_text(terminated_xml, "<text:p", "</text:p>", "</text:p>");
    free(terminated_xml);
    return text;
}

char *doc_extract_epub_text(const char *path)
{
    if (!path) {
        return NULL;
    }
    mz_zip_archive zip;
    memset(&zip, 0, sizeof(zip));
    if (!mz_zip_reader_init_file(&zip, path, 0)) {
        return NULL;
    }
    mz_uint num_files = mz_zip_reader_get_num_files(&zip);
    size_t total_alloc = 65536;
    size_t total_len = 0;
    char *result = malloc(total_alloc);
    if (!result) {
        mz_zip_reader_end(&zip);
        return NULL;
    }
    result[0] = '\0';

    for (mz_uint i = 0; i < num_files; i++) {
        mz_zip_archive_file_stat stat;
        if (!mz_zip_reader_file_stat(&zip, i, &stat)) {
            continue;
        }
        const char *fn = stat.m_filename;
        const char *dot = strrchr(fn, '.');
        if (!dot) {
            continue;
        }
        if (strcasecmp(dot, ".xhtml") != 0 && strcasecmp(dot, ".html") != 0 &&
            strcasecmp(dot, ".htm") != 0) {
            continue;
        }
        size_t file_sz = 0;
        char *html = (char *)mz_zip_reader_extract_to_heap(&zip, i, &file_sz, 0);
        if (!html) {
            continue;
        }
        char *term_html = malloc(file_sz + 1);
        if (!term_html) {
            mz_free(html);
            continue;
        }
        memcpy(term_html, html, file_sz);
        term_html[file_sz] = '\0';
        mz_free(html);

        char *plain = doc_extract_html_text(term_html);
        free(term_html);
        if (plain) {
            size_t plen = strlen(plain);
            if (total_len + plen + 4 > total_alloc) {
                total_alloc = (total_alloc + plen + 4) * 2;
                char *grown = realloc(result, total_alloc);
                if (grown) {
                    result = grown;
                } else {
                    free(plain);
                    break;
                }
            }
            if (total_len > 0) {
                result[total_len++] = '\n';
                result[total_len++] = '\n';
            }
            memcpy(result + total_len, plain, plen);
            total_len += plen;
            result[total_len] = '\0';
            free(plain);
        }
    }
    mz_zip_reader_end(&zip);
    if (total_len == 0) {
        free(result);
        return NULL;
    }
    return result;
}

static char *doc_extract_pdf_stream_fallback(const char *path)
{
    char *raw = read_entire_plain_file(path);
    if (!raw) {
        return NULL;
    }
    size_t raw_len = strlen(raw);
    size_t out_cap = 65536;
    size_t out_len = 0;
    char *out = malloc(out_cap);
    if (!out) {
        free(raw);
        return NULL;
    }
    out[0] = '\0';

    const char *p = raw;
    while (p < raw + raw_len) {
        const char *s = strstr(p, "stream");
        if (!s) {
            break;
        }
        const char *s_end = strstr(s, "endstream");
        if (!s_end) {
            break;
        }
        const char *s_data = s + 6;
        while (s_data < s_end && (*s_data == '\r' || *s_data == '\n')) {
            s_data++;
        }
        if (s_data >= s_end) {
            p = s_end + 9;
            continue;
        }
        size_t s_len = (size_t)(s_end - s_data);

        /* Check if preceded by /FlateDecode */
        const char *dict_start = s > raw + 256 ? s - 256 : raw;
        bool is_flate = (strstr(dict_start, "/FlateDecode") != NULL && strstr(dict_start, "/FlateDecode") < s);

        const char *decomp = NULL;
        size_t decomp_len = 0;
        char *flate_buf = NULL;

        if (is_flate && s_len > 2) {
            mz_ulong uncomp_len = (mz_ulong)(s_len * 10 + 4096);
            flate_buf = malloc(uncomp_len);
            if (flate_buf && mz_uncompress((unsigned char *)flate_buf, &uncomp_len,
                                           (const unsigned char *)s_data, (mz_ulong)s_len) == MZ_OK) {
                decomp = flate_buf;
                decomp_len = uncomp_len;
            }
        }
        if (!decomp) {
            decomp = s_data;
            decomp_len = s_len;
        }

        /* Scan decompressed content for PDF string literals (text) */
        const char *d = decomp;
        const char *d_end = decomp + decomp_len;
        while (d < d_end) {
            if (*d == '(') {
                d++;
                const char *str_start = d;
                int depth = 1;
                while (d < d_end && depth > 0) {
                    if (*d == '\\') {
                        d += 2;
                        continue;
                    }
                    if (*d == '(') {
                        depth++;
                    } else if (*d == ')') {
                        depth--;
                    }
                    if (depth > 0) {
                        d++;
                    }
                }
                size_t slen = (size_t)(d - str_start);
                if (slen > 0) {
                    if (out_len + slen + 2 > out_cap) {
                        out_cap = (out_cap + slen + 2) * 2;
                        char *grown = realloc(out, out_cap);
                        if (grown) {
                            out = grown;
                        } else {
                            break;
                        }
                    }
                    memcpy(out + out_len, str_start, slen);
                    out_len += slen;
                    out[out_len++] = ' ';
                    out[out_len] = '\0';
                }
            }
            d++;
        }

        free(flate_buf);
        p = s_end + 9;
    }

    free(raw);
    if (out_len == 0) {
        free(out);
        return NULL;
    }
    return out;
}

char *doc_extract_text(const char *file_path)
{
    if (!file_path) {
        return NULL;
    }

    if (doc_is_git_lfs_pointer(file_path)) {
        logger_info("Skipping Git LFS pointer: %s (run 'git lfs pull' to fetch file)", file_path);
        return NULL;
    }

    const char *ext = strrchr(file_path, '.');
    if (ext) {
        ext++; /* skip '.' */
    }

    if (ext && doc_is_ignored_extension(ext)) {
        return NULL;
    }

    char *raw_text = NULL;

    if (ext && strcasecmp(ext, "pdf") == 0) {
        /* Check %PDF- header */
        FILE *fp = fopen(file_path, "rb");
        if (!fp) {
            return NULL;
        }
        char magic[8] = {0};
        size_t n = fread(magic, 1, 5, fp);
        fclose(fp);
        if (n < 5 || strncmp(magic, "%PDF-", 5) != 0) {
            logger_warn("Could not extract PDF '%s' (invalid %%PDF- header or corrupt file)",
                        file_path);
            return NULL;
        }

        char *escaped = doc_shell_escape(file_path);
        if (escaped) {
            char cmd[4096];
            snprintf(cmd, sizeof(cmd), "pdftotext -q -nopgbrk %s - 2>/dev/null", escaped);
            free(escaped);

            FILE *p = popen(cmd, "r");
            if (p) {
                raw_text = read_pipe_output(p);
                int rc = pclose(p);
                if (rc != 0 || !raw_text || raw_text[0] == '\0') {
                    free(raw_text);
                    raw_text = NULL;
                }
            }
        }

        if (!raw_text) {
            /* Pure C stream extraction fallback */
            raw_text = doc_extract_pdf_stream_fallback(file_path);
        }
    } else if (ext && strcasecmp(ext, "docx") == 0) {
        raw_text = doc_extract_docx_text(file_path);
    } else if (ext && strcasecmp(ext, "odt") == 0) {
        raw_text = doc_extract_odt_text(file_path);
    } else if (ext && strcasecmp(ext, "epub") == 0) {
        raw_text = doc_extract_epub_text(file_path);
    } else if (ext && (strcasecmp(ext, "html") == 0 || strcasecmp(ext, "htm") == 0)) {
        char *html_content = read_entire_plain_file(file_path);
        if (html_content) {
            raw_text = doc_extract_html_text(html_content);
            free(html_content);
        }
    } else if (ext && strcasecmp(ext, "doc") == 0) {
        char *escaped = doc_shell_escape(file_path);
        if (escaped) {
            char cmd[4096];
            snprintf(cmd, sizeof(cmd), "antiword -t %s 2>/dev/null", escaped);
            free(escaped);

            FILE *p = popen(cmd, "r");
            if (p) {
                raw_text = read_pipe_output(p);
                pclose(p);
            }
        }
    } else {
        /* Plain text, Markdown, RST, Org, TeX, AsciiDoc */
        if (doc_is_binary_file(file_path)) {
            return NULL;
        }
        raw_text = read_entire_plain_file(file_path);
    }

    if (!raw_text) {
        return NULL;
    }

    /* Sanitize UTF-8 in-place to prevent llama_tokenize from ever receiving invalid codepoints */
    utf8_sanitize(raw_text);

    /* Verify there is at least some non-whitespace content */
    const unsigned char *chk = (const unsigned char *)raw_text;
    bool has_content = false;
    while (*chk) {
        if (!isspace((int)*chk)) {
            has_content = true;
            break;
        }
        chk++;
    }

    if (!has_content) {
        free(raw_text);
        return NULL;
    }

    return raw_text;
}

void doc_clean_path(const char *in, char *out, size_t out_sz)
{
    if (!out || out_sz == 0) {
        return;
    }
    if (!in) {
        out[0] = '\0';
        return;
    }

    /* 1. Skip leading whitespace */
    const char *start = in;
    while (*start && isspace((unsigned char)*start)) {
        start++;
    }

    /* 2. Find end of string, trim trailing whitespace */
    size_t in_len = strlen(start);
    const char *end = start + in_len;
    while (end > start && isspace((unsigned char)*(end - 1))) {
        end--;
    }

    /* 3. Strip matching outer quotes: "path" or 'path' */
    if ((size_t)(end - start) >= 2 &&
        ((*start == '"' && *(end - 1) == '"') || (*start == '\'' && *(end - 1) == '\''))) {
        start++;
        end--;
        while (start < end && isspace((unsigned char)*start)) {
            start++;
        }
        while (end > start && isspace((unsigned char)*(end - 1))) {
            end--;
        }
    }

    /* 4. Unescape backslashes (e.g. "\ " -> " ", "\\ " -> "\ ", etc.) */
    char unescaped[2048];
    size_t ulen = 0;
    for (const char *p = start; p < end && ulen + 1 < sizeof(unescaped); p++) {
        if (*p == '\\' && (p + 1 < end) &&
            (*(p + 1) == ' ' || *(p + 1) == '\\' || *(p + 1) == '\"' || *(p + 1) == '\'')) {
            unescaped[ulen++] = *(p + 1);
            p++;
        } else {
            unescaped[ulen++] = *p;
        }
    }
    unescaped[ulen] = '\0';

    /* 5. Tilde expansion: "~" or "~/" -> "$HOME" */
    if (unescaped[0] == '~' && (unescaped[1] == '/' || unescaped[1] == '\0')) {
        const char *home = getenv("HOME");
        if (home) {
            snprintf(out, out_sz, "%s%s", home, unescaped + 1);
            return;
        }
    }

    snprintf(out, out_sz, "%s", unescaped);
}
