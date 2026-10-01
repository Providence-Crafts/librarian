#include "minunit.h"
#include "db.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <math.h>

const char *test_db_lifecycle_and_knn(void);

const char *test_db_lifecycle_and_knn(void)
{
    const char *test_db_path = "/tmp/test_librarian.db";
    unlink(test_db_path);

    db_context_t *db = db_open(test_db_path);
    mu_assert("db_open failed", db != NULL);

    int rc = db_init_schema(db, 4);
    mu_assert("db_init_schema failed", rc == 0);

    /* Begin transaction */
    rc = db_begin_transaction(db);
    mu_assert("db_begin_transaction failed", rc == 0);

    int64_t doc_id = db_insert_document(db, "docs/test.txt");
    mu_assert("db_insert_document failed", doc_id > 0);

    /* Insert chunk 1 with vector [1.0, 0.0, 0.0, 0.0] */
    float vec1[4] = {1.0f, 0.0f, 0.0f, 0.0f};
    rc = db_insert_chunk(db, doc_id, 0, "First chunk about apples", vec1, 4);
    mu_assert("db_insert_chunk 1 failed", rc == 0);

    /* Insert chunk 2 with vector [0.0, 1.0, 0.0, 0.0] */
    float vec2[4] = {0.0f, 1.0f, 0.0f, 0.0f};
    rc = db_insert_chunk(db, doc_id, 1, "Second chunk about bananas", vec2, 4);
    mu_assert("db_insert_chunk 2 failed", rc == 0);

    rc = db_commit_transaction(db);
    mu_assert("db_commit_transaction failed", rc == 0);

    /* Verify stats */
    int doc_count = 0, chunk_count = 0;
    rc = db_get_stats(db, &doc_count, &chunk_count);
    mu_assert("db_get_stats failed", rc == 0);
    mu_assert("doc_count should be 1", doc_count == 1);
    mu_assert("chunk_count should be 2", chunk_count == 2);

    /* Search for vector close to vec1: [0.99, 0.01, 0.0, 0.0] */
    float query[4] = {0.99f, 0.01f, 0.0f, 0.0f};
    search_result_t *results = NULL;
    int result_count = 0;

    rc = db_search_knn(db, query, 4, 2, &results, &result_count);
    mu_assert("db_search_knn failed", rc == 0);
    mu_assert("result_count should be 2", result_count == 2);
    mu_assert("results should not be null", results != NULL);

    /* Top result should be chunk 1 (apples) */
    mu_assert("top result should be apples",
              strstr(results[0].content, "apples") != NULL);
    mu_assert("top result similarity should be > 0.9",
              results[0].similarity > 0.9f);

    /* Second result should be chunk 2 (bananas) */
    mu_assert("second result should be bananas",
              strstr(results[1].content, "bananas") != NULL);
    mu_assert("second result similarity should be lower than top",
              results[1].similarity < results[0].similarity);

    db_free_results(results, result_count);
    db_close(db);

    unlink(test_db_path);
    return NULL;
}
