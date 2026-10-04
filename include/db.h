#ifndef LIBRARIAN_DB_H
#define LIBRARIAN_DB_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct db_context db_context_t;

typedef struct search_result {
    int64_t chunk_id;
    int64_t doc_id;
    int chunk_idx;
    char doc_path[512];
    char *content;
    float distance;
    float similarity; /* S = 1.0f - distance */
} search_result_t;

db_context_t *db_open(const char *path);
void db_close(db_context_t *db);

int db_init_schema(db_context_t *db, int embed_dim);
int db_reset(db_context_t *db, int embed_dim);

int db_begin_transaction(db_context_t *db);
int db_commit_transaction(db_context_t *db);
int db_rollback_transaction(db_context_t *db);

uint64_t hash_fnv1a64(const void *data, size_t len);
bool db_hash_file_fnv1a64(const char *path, char *out_hash, size_t out_hash_sz);

int64_t db_insert_document(db_context_t *db, const char *path);
int64_t db_insert_document_with_hash(db_context_t *db, const char *path, const char *content_hash);
bool db_find_document_by_hash(db_context_t *db, const char *content_hash, int64_t *out_doc_id);
bool db_get_document_hash_by_path(db_context_t *db, const char *path, char *out_hash,
                                  size_t out_hash_sz);
typedef void (*db_doc_hash_cb)(const char *path, const char *content_hash, void *user_data);
int db_for_each_document_hash(db_context_t *db, db_doc_hash_cb callback, void *user_data);
int db_delete_document_chunks(db_context_t *db, int64_t doc_id);

int db_insert_chunk(db_context_t *db, int64_t doc_id, int chunk_idx, const char *content,
                    const float *vec, int dim);

int db_search_knn(db_context_t *db, const float *query_vec, int dim, int k,
                  search_result_t **out_results, int *out_count);
void db_free_results(search_result_t *results, int count);

int db_get_stats(db_context_t *db, int *out_doc_count, int *out_chunk_count);

typedef struct {
    int64_t id;
    char path[512];
    int chunk_count;
    char content_hash[32];
} doc_info_t;

typedef struct {
    int64_t id;
    int chunk_idx;
    int word_count;
    char *content;
} chunk_info_t;

int db_list_documents(db_context_t *db, const char *search_pattern, doc_info_t **out_docs,
                      int *out_count);
void db_free_doc_info(doc_info_t *docs, int count);

int db_get_document_chunks(db_context_t *db, int64_t doc_id, chunk_info_t **out_chunks,
                           int *out_count);
void db_free_chunk_info(chunk_info_t *chunks, int count);

#endif /* LIBRARIAN_DB_H */
