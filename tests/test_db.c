#include "db.h"
#include "minunit.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

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
    mu_assert("top result should be apples", strstr(results[0].content, "apples") != NULL);
    mu_assert("top result similarity should be > 0.9", results[0].similarity > 0.9f);
    mu_assert("top result chunk_idx should be 0", results[0].chunk_idx == 0);

    /* Second result should be chunk 2 (bananas) */
    mu_assert("second result should be bananas", strstr(results[1].content, "bananas") != NULL);
    mu_assert("second result similarity should be lower than top",
              results[1].similarity < results[0].similarity);
    mu_assert("second result chunk_idx should be 1", results[1].chunk_idx == 1);

    db_free_results(results, result_count);

    /* Test content hashing and hash deduplication */
    const char *doc_content = "Hello Librarian Knowledge Base";
    uint64_t h1 = hash_fnv1a64(doc_content, strlen(doc_content));
    uint64_t h2 = hash_fnv1a64(doc_content, strlen(doc_content));
    mu_assert("hash_fnv1a64 should be deterministic", h1 == h2);
    mu_assert("hash_fnv1a64 should not be 0", h1 != 0);

    char hstr[32];
    snprintf(hstr, sizeof(hstr), "%016llx", (unsigned long long)h1);

    int64_t hash_doc_id = db_insert_document_with_hash(db, "docs/hashed.txt", hstr);
    mu_assert("db_insert_document_with_hash should succeed", hash_doc_id > 0);

    int64_t found_id = -1;
    bool found = db_find_document_by_hash(db, hstr, &found_id);
    mu_assert("db_find_document_by_hash should find inserted doc",
              found && found_id == hash_doc_id);

    char retrieved_hash[32] = {0};
    bool path_found =
        db_get_document_hash_by_path(db, "docs/hashed.txt", retrieved_hash, sizeof(retrieved_hash));
    mu_assert("db_get_document_hash_by_path should succeed", path_found);
    mu_assert("retrieved hash should match", strcmp(retrieved_hash, hstr) == 0);

    /* Test database reset */
    rc = db_reset(db, 4);
    mu_assert("db_reset should return 0", rc == 0);

    int post_reset_docs = -1, post_reset_chunks = -1;
    rc = db_get_stats(db, &post_reset_docs, &post_reset_chunks);
    mu_assert("db_get_stats after reset should succeed", rc == 0);
    mu_assert("docs after reset should be 0", post_reset_docs == 0);
    mu_assert("chunks after reset should be 0", post_reset_chunks == 0);

    /* Verify database is fully functional after reset */
    int64_t fresh_doc_id = db_insert_document(db, "docs/fresh.txt");
    mu_assert("inserting doc after reset should succeed", fresh_doc_id > 0);
    float fresh_vec[4] = {0.5f, 0.5f, 0.5f, 0.5f};
    rc = db_insert_chunk(db, fresh_doc_id, 0, "Fresh chunk content", fresh_vec, 4);
    mu_assert("inserting chunk after reset should succeed", rc == 0);

    db_get_stats(db, &post_reset_docs, &post_reset_chunks);
    mu_assert("docs after fresh insert should be 1", post_reset_docs == 1);
    mu_assert("chunks after fresh insert should be 1", post_reset_chunks == 1);

    /* Test db_list_documents */
    doc_info_t *docs = NULL;
    int doc_count_listed = 0;
    rc = db_list_documents(db, NULL, &docs, &doc_count_listed);
    mu_assert("db_list_documents should succeed", rc == 0);
    mu_assert("doc_count_listed should be 1", doc_count_listed == 1);
    mu_assert("listed doc path match", strcmp(docs[0].path, "docs/fresh.txt") == 0);
    mu_assert("listed doc chunk count should be 1", docs[0].chunk_count == 1);
    db_free_doc_info(docs, doc_count_listed);

    /* Test db_list_documents with filter */
    rc = db_list_documents(db, "fresh", &docs, &doc_count_listed);
    mu_assert("db_list_documents with filter should succeed", rc == 0 && doc_count_listed == 1);
    db_free_doc_info(docs, doc_count_listed);

    rc = db_list_documents(db, "nonexistent", &docs, &doc_count_listed);
    mu_assert("db_list_documents with nonexistent filter should return 0", rc == 0 && doc_count_listed == 0);
    db_free_doc_info(docs, doc_count_listed);

    /* Test db_get_document_chunks */
    chunk_info_t *chunks = NULL;
    int chunk_count_listed = 0;
    rc = db_get_document_chunks(db, fresh_doc_id, &chunks, &chunk_count_listed);
    mu_assert("db_get_document_chunks should succeed", rc == 0);
    mu_assert("chunk_count_listed should be 1", chunk_count_listed == 1);
    mu_assert("chunk_idx should be 0", chunks[0].chunk_idx == 0);
    mu_assert("chunk content should match", strcmp(chunks[0].content, "Fresh chunk content") == 0);
    mu_assert("chunk word count should be 3", chunks[0].word_count == 3);
    db_free_chunk_info(chunks, chunk_count_listed);

    db_close(db);

    unlink(test_db_path);
    return NULL;
}
