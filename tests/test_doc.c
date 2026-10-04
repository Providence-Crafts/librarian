#include "doc.h"
#include "embedder.h"
#include "minunit.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

const char *test_doc_extension_filtering(void);
const char *test_doc_shell_escaping(void);
const char *test_doc_utf8_sanitizer(void);
const char *test_doc_text_extraction(void);

const char *test_doc_extension_filtering(void)
{
    mu_assert("pdf should be supported", doc_is_supported_extension("pdf"));
    mu_assert("PDF (uppercase) should be supported", doc_is_supported_extension("PDF"));
    mu_assert("epub should be supported", doc_is_supported_extension("epub"));
    mu_assert("odt should be supported", doc_is_supported_extension("odt"));
    mu_assert("docx should be supported", doc_is_supported_extension("docx"));
    mu_assert("doc should be supported", doc_is_supported_extension("doc"));
    mu_assert("html should be supported", doc_is_supported_extension("html"));
    mu_assert("md should be supported", doc_is_supported_extension("md"));
    mu_assert("txt should be supported", doc_is_supported_extension("txt"));
    mu_assert("py should NOT be supported", !doc_is_supported_extension("py"));
    mu_assert("json should NOT be supported", !doc_is_supported_extension("json"));
    mu_assert("bib should NOT be supported", !doc_is_supported_extension("bib"));

    mu_assert("zip should be ignored", doc_is_ignored_extension("zip"));
    mu_assert("tar should be ignored", doc_is_ignored_extension("tar"));
    mu_assert("pyc should be ignored", doc_is_ignored_extension("pyc"));
    mu_assert("png should be ignored", doc_is_ignored_extension("png"));
    mu_assert("xlsm should be ignored", doc_is_ignored_extension("xlsm"));
    mu_assert("xlsx should be ignored", doc_is_ignored_extension("xlsx"));

    mu_assert("pdf should NOT be ignored", !doc_is_ignored_extension("pdf"));
    mu_assert("md should NOT be ignored", !doc_is_ignored_extension("md"));

    return NULL;
}

const char *test_doc_shell_escaping(void)
{
    char *esc1 = doc_shell_escape("simple.txt");
    mu_assert("esc1 not null", esc1 != NULL);
    mu_assert("esc1 expected 'simple.txt'", strcmp(esc1, "'simple.txt'") == 0);
    free(esc1);

    char *esc2 = doc_shell_escape("file with spaces & ampersand.pdf");
    mu_assert("esc2 not null", esc2 != NULL);
    mu_assert("esc2 expected quote wrapper",
              strcmp(esc2, "'file with spaces & ampersand.pdf'") == 0);
    free(esc2);

    char *esc3 = doc_shell_escape("O'Reilly's Book.epub");
    mu_assert("esc3 not null", esc3 != NULL);
    mu_assert("esc3 expected escaped quotes", strcmp(esc3, "'O'\\''Reilly'\\''s Book.epub'") == 0);
    free(esc3);

    return NULL;
}

const char *test_doc_utf8_sanitizer(void)
{
    /* Test 1: Plain ASCII */
    char t1[] = "Hello World 123!";
    utf8_sanitize(t1);
    mu_assert("t1 unchanged", strcmp(t1, "Hello World 123!") == 0);

    /* Test 2: Valid UTF-8 multi-byte */
    char t2[] = "Café & München & 📚";
    utf8_sanitize(t2);
    mu_assert("t2 unchanged", strcmp(t2, "Café & München & 📚") == 0);

    /* Test 3: Invalid isolated byte > 0x80 */
    char t3[] = "Bad\x80"
                "Byte";
    utf8_sanitize(t3);
    mu_assert("t3 sanitized", strcmp(t3, "Bad Byte") == 0);

    /* Test 4: Overlong encoding 0xC0 0x80 (NUL overlong) */
    char t4[] = "Over\xC0\x80Long";
    utf8_sanitize(t4);
    mu_assert("t4 sanitized", strcmp(t4, "Over  Long") == 0);

    /* Test 5: Surrogate halves 0xED 0xA0 0x80 (U+D800) */
    char t5[] = "Surr\xED\xA0\x80Gate";
    utf8_sanitize(t5);
    mu_assert("t5 sanitized", strcmp(t5, "Surr   Gate") == 0);

    /* Test 6: Codepoint > 0x10FFFF (0xF4 0x90 0x80 0x80) */
    char t6[] = "High\xF4\x90\x80\x80Point";
    utf8_sanitize(t6);
    mu_assert("t6 sanitized", strcmp(t6, "High    Point") == 0);

    /* Test 7: Non-printable control characters (e.g. 0x01, 0x02) */
    char t7[] = "Ctrl\x01\x02"
                "Chars";
    utf8_sanitize(t7);
    mu_assert("t7 sanitized", strcmp(t7, "Ctrl  Chars") == 0);

    return NULL;
}

const char *test_doc_text_extraction(void)
{
    /* Create a temporary text file */
    const char *tmp_txt = "/tmp/test_librarian_doc.txt";
    FILE *fp = fopen(tmp_txt, "w");
    mu_assert("create tmp_txt", fp != NULL);
    fprintf(
        fp,
        "This is a document extraction test for Librarian.\nWith multiple lines of knowledge.\n");
    fclose(fp);

    char *extracted = doc_extract_text(tmp_txt);
    mu_assert("extracted not null", extracted != NULL);
    mu_assert("extracted contains test phrase",
              strstr(extracted, "document extraction test") != NULL);
    free(extracted);
    remove(tmp_txt);

    /* Test binary file detection: create file with null byte */
    const char *tmp_bin = "/tmp/test_librarian_bin.dat";
    fp = fopen(tmp_bin, "wb");
    mu_assert("create tmp_bin", fp != NULL);
    unsigned char bin_bytes[] = {'P', 'D', 'F', 0, 1, 2, 3};
    fwrite(bin_bytes, 1, sizeof(bin_bytes), fp);
    fclose(fp);

    mu_assert("tmp_bin detected as binary", doc_is_binary_file(tmp_bin));
    char *bin_extracted = doc_extract_text(tmp_bin);
    mu_assert("binary file extraction returns NULL", bin_extracted == NULL);
    remove(tmp_bin);

    return NULL;
}

const char *test_doc_git_lfs_and_html(void)
{
    /* Test 1: HTML text extraction */
    const char *sample_html =
        "<html><head><script>console.log('secret');</script><style>p { color: red; }</style></head>"
        "<body><h1>Librarian Engine</h1><p>Knowledge is &quot;power&quot; &amp; freedom &nbsp;!</p></body></html>";
    char *html_text = doc_extract_html_text(sample_html);
    mu_assert("html_text not null", html_text != NULL);
    mu_assert("script was stripped", strstr(html_text, "console.log") == NULL);
    mu_assert("style was stripped", strstr(html_text, "color: red") == NULL);
    mu_assert("h1 extracted", strstr(html_text, "Librarian Engine") != NULL);
    mu_assert("entities decoded", strstr(html_text, "Knowledge is \"power\" & freedom  !") != NULL);
    free(html_text);

    /* Test 2: Git LFS pointer detection */
    const char *tmp_lfs = "/tmp/test_librarian_lfs.pdf";
    FILE *fp = fopen(tmp_lfs, "w");
    mu_assert("create tmp_lfs", fp != NULL);
    fprintf(fp, "version https://git-lfs.github.com/spec/v1\n"
                "oid sha256:d8811b7b9b68cad41935d2c354381332f0c69aba768cee5c9986ebcb56d23931\n"
                "size 61314833\n");
    fclose(fp);

    mu_assert("detected as git lfs pointer", doc_is_git_lfs_pointer(tmp_lfs));
    char *lfs_extracted = doc_extract_text(tmp_lfs);
    mu_assert("lfs pointer extraction safely returns NULL", lfs_extracted == NULL);
    remove(tmp_lfs);

    return NULL;
}

const char *test_doc_clean_path(void)
{
    char out[1024];
    const char *home = getenv("HOME");
    if (!home) {
        home = "/";
    }

    /* 1. Trailing whitespace from tab completion */
    doc_clean_path("~/notes/category_theory/Tom Leinster.pdf ", out, sizeof(out));
    char expected[1024];
    snprintf(expected, sizeof(expected), "%s/notes/category_theory/Tom Leinster.pdf", home);
    mu_assert("trailing space should be trimmed", strcmp(out, expected) == 0);

    /* 2. Leading whitespace */
    doc_clean_path("   /tmp/data.txt", out, sizeof(out));
    mu_assert("leading space trimmed", strcmp(out, "/tmp/data.txt") == 0);

    /* 3. Double quotes */
    doc_clean_path("\"~/notes/category_theory/Tom Leinster.pdf\"", out, sizeof(out));
    mu_assert("double quotes stripped and expanded", strcmp(out, expected) == 0);

    /* 4. Single quotes */
    doc_clean_path("'~/notes/category_theory/Tom Leinster.pdf'", out, sizeof(out));
    mu_assert("single quotes stripped and expanded", strcmp(out, expected) == 0);

    /* 5. Escaped spaces */
    doc_clean_path("~/notes/category_theory/Tom\\ Leinster.pdf", out, sizeof(out));
    mu_assert("escaped spaces unescaped and expanded", strcmp(out, expected) == 0);

    /* 6. Empty / NULL input */
    doc_clean_path("", out, sizeof(out));
    mu_assert("empty input yields empty", strcmp(out, "") == 0);
    doc_clean_path(NULL, out, sizeof(out));
    mu_assert("null input yields empty", strcmp(out, "") == 0);

    return NULL;
}
