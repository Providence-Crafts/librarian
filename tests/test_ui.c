#include "minunit.h"
#include "ui.h"

#include <string.h>

const char *test_ui_badges(void);

const char *test_ui_badges(void)
{
    /* Test that UI functions run without crashing or leaking */
    ui_banner();
    ui_prompt();
    ui_status("[testing...]");
    ui_ingest_progress(5, 10, "/home/test/sample.txt");
    ui_clear_status();
    ui_confidence_badge(0.85f, false);
    ui_confidence_badge(0.35f, true);
    ui_similarity_badge(0.72f, 0.65f);
    ui_info("Test info message %d", 42);
    ui_success("Test success message");
    ui_warn("Test warning message");
    ui_error("Test error message");

    char c1[] =
        "A solar cell converts sunlight into electrical current via the photovoltaic effect.";
    char c2[] = "Semiconductor materials absorb photons and release electrons.";
    search_result_t dummy_results[2] = {
        {.chunk_id = 1,
         .doc_id = 10,
         .chunk_idx = 0,
         .doc_path = "/home/test/docs/photovoltaics.md",
         .content = c1,
         .distance = 0.15f,
         .similarity = 0.85f},
        {.chunk_id = 2,
         .doc_id = 10,
         .chunk_idx = 1,
         .doc_path = "/home/test/docs/photovoltaics.md",
         .content = c2,
         .distance = 0.50f,
         .similarity = 0.50f},
    };
    ui_references(dummy_results, 2, 0.65f);
    ui_references(NULL, 0, 0.65f);

    doc_info_t dummy_docs[2] = {
        {.id = 1, .path = "/home/test/docs/solar.pdf", .chunk_count = 42, .content_hash = "abc123"},
        {.id = 2, .path = "/home/test/docs/notes.md", .chunk_count = 5, .content_hash = "def456"},
    };
    ui_list_documents(dummy_docs, 2, NULL);
    ui_list_documents(dummy_docs, 2, "solar");
    ui_list_documents(NULL, 0, "none");

    chunk_info_t dummy_chunks[2] = {
        {.id = 1, .chunk_idx = 0, .word_count = 250, .content = c1},
        {.id = 2, .chunk_idx = 1, .word_count = 180, .content = c2},
    };
    ui_list_chunks(1, "/home/test/docs/solar.pdf", dummy_chunks, 2, 10);
    ui_list_chunks(1, NULL, NULL, 0, 10);

    return NULL;
}
