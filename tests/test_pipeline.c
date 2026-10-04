#include "db.h"
#include "embedder.h"
#include "minunit.h"
#include "pipeline.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

const char *test_pipeline_concurrent_ingest(void);

const char *test_pipeline_concurrent_ingest(void)
{
    const char *model_path = "models/harrier-oss-v1-0.6b.Q8_0.gguf";
    FILE *mf = fopen(model_path, "rb");
    if (!mf) {
        printf("       [SKIP] Embedder model %s not found\n", model_path);
        return NULL;
    }
    fclose(mf);

    const char *db_path = "/tmp/test_librarian_pipeline.db";
    unlink(db_path);

    db_context_t *db = db_open(db_path);
    mu_assert("db_open failed in pipeline test", db != NULL);
    mu_assert("db_init_schema failed in pipeline test", db_init_schema(db, 1024) == 0);

    embedder_context_t *emb = embedder_init(model_path, 1024);
    mu_assert("embedder_init failed in pipeline test", emb != NULL);

    /* Create temporary files for testing concurrent ingestion */
    char f1[] = "/tmp/lib_pipe_test_1.txt";
    char f2[] = "/tmp/lib_pipe_test_2.txt";
    char f3[] = "/tmp/lib_pipe_test_3.txt";

    FILE *fp1 = fopen(f1, "w");
    fprintf(fp1, "Librarian is a self-contained local RAG system implemented in pure C99.\n");
    fclose(fp1);

    FILE *fp2 = fopen(f2, "w");
    fprintf(fp2, "Vulkan acceleration allows sub-second embedding and high throughput.\n");
    fclose(fp2);

    FILE *fp3 = fopen(f3, "w");
    fprintf(fp3, "Multi-threaded pipelined ingestion extracts documents concurrently.\n");
    fclose(fp3);

    const char *f4 = "/tmp/test_pipeline_empty.txt";
    FILE *fp4 = fopen(f4, "w");
    /* Empty file: 0 words -> extract_failed */
    fclose(fp4);

    char *paths[4] = {f1, f2, f3, (char *)f4};
    int docs = 0, chunks = 0, skipped = 0, failed = 0;

    int rc =
        pipeline_ingest_files(db, emb, paths, 4, 1024, 250, 40, &docs, &chunks, &skipped, &failed);
    mu_assert("pipeline_ingest_files failed", rc == 0);
    mu_assert("docs should be 3", docs == 3);
    mu_assert("chunks should be >= 3", chunks >= 3);
    mu_assert("skipped should be 0", skipped == 0);
    mu_assert("failed should be 1 for empty doc", failed == 1);

    /* Second run: all 3 valid docs should be skipped via in-memory hash cache, f4 fails again */
    docs = 0;
    chunks = 0;
    skipped = 0;
    failed = 0;
    rc = pipeline_ingest_files(db, emb, paths, 4, 1024, 250, 40, &docs, &chunks, &skipped, &failed);
    mu_assert("pipeline_ingest_files second run failed", rc == 0);
    mu_assert("second run docs should be 0", docs == 0);
    mu_assert("second run chunks should be 0", chunks == 0);
    mu_assert("second run skipped should be 3", skipped == 3);
    mu_assert("second run failed should be 1", failed == 1);

    embedder_free(emb);
    db_close(db);

    unlink(f1);
    unlink(f2);
    unlink(f3);
    unlink(f4);
    unlink(db_path);

    return NULL;
}
