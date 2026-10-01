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

    return NULL;
}
