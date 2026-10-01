#ifndef LIBRARIAN_DB_H
#define LIBRARIAN_DB_H

#include <stdint.h>
#include <stdbool.h>

typedef struct db_context db_context_t;

typedef struct {
    int64_t chunk_id;
    int64_t doc_id;
    char doc_path[512];
    char *content;
    float distance;
    float similarity; /* S = 1.0f - distance */
} search_result_t;

db_context_t *db_open(const char *path);
void db_close(db_context_t *db);

int db_init_schema(db_context_t *db, int embed_dim);

int db_begin_transaction(db_context_t *db);
int db_commit_transaction(db_context_t *db);
int db_rollback_transaction(db_context_t *db);

int64_t db_insert_document(db_context_t *db, const char *path);
int db_insert_chunk(db_context_t *db, int64_t doc_id, int chunk_idx, const char *content, const float *vec, int dim);

int db_search_knn(db_context_t *db, const float *query_vec, int dim, int k, search_result_t **out_results, int *out_count);
void db_free_results(search_result_t *results, int count);

int db_get_stats(db_context_t *db, int *out_doc_count, int *out_chunk_count);

#endif /* LIBRARIAN_DB_H */
