#include "db.h"

#include "sqlite-vec.h"
#include "sqlite3.h"

#include <libgen.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>

struct db_context {
    sqlite3 *handle;
    int embed_dim;
};

static void ensure_dir_exists(const char *file_path)
{
    char tmp[512];
    strncpy(tmp, file_path, sizeof(tmp) - 1);
    tmp[sizeof(tmp) - 1] = '\0';
    char *dir = dirname(tmp);
    if (dir && strcmp(dir, ".") != 0 && strcmp(dir, "/") != 0) {
        mkdir(dir, 0755);
    }
}

db_context_t *db_open(const char *path)
{
    /* Register sqlite-vec auto extension if not already registered */
    int rc = sqlite3_auto_extension((void (*)(void))sqlite3_vec_init);
    if (rc != SQLITE_OK && rc != SQLITE_MISUSE) {
        fprintf(stderr, "Failed to register sqlite-vec extension: %d\n", rc);
        return NULL;
    }

    ensure_dir_exists(path);

    sqlite3 *db = NULL;
    rc = sqlite3_open_v2(path, &db, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, NULL);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "Failed to open SQLite database %s: %s\n", path, sqlite3_errmsg(db));
        if (db)
            sqlite3_close(db);
        return NULL;
    }

    /* Configure SQLite performance pragmas */
    char *err = NULL;
    sqlite3_exec(db, "PRAGMA journal_mode = WAL;", NULL, NULL, &err);
    if (err)
        sqlite3_free(err);
    sqlite3_exec(db, "PRAGMA synchronous = NORMAL;", NULL, NULL, &err);
    if (err)
        sqlite3_free(err);
    sqlite3_exec(db, "PRAGMA foreign_keys = ON;", NULL, NULL, &err);
    if (err)
        sqlite3_free(err);

    db_context_t *ctx = calloc(1, sizeof(*ctx));
    if (!ctx) {
        sqlite3_close(db);
        return NULL;
    }
    ctx->handle = db;
    return ctx;
}

void db_close(db_context_t *db)
{
    if (!db)
        return;
    if (db->handle) {
        sqlite3_close(db->handle);
    }
    free(db);
}

int db_init_schema(db_context_t *db, int embed_dim)
{
    if (!db || !db->handle)
        return -1;
    db->embed_dim = embed_dim;

    const char *sql_docs = "CREATE TABLE IF NOT EXISTS documents ("
                           "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
                           "  path TEXT UNIQUE NOT NULL,"
                           "  created_at DATETIME DEFAULT CURRENT_TIMESTAMP"
                           ");";

    const char *sql_chunks = "CREATE TABLE IF NOT EXISTS chunks ("
                             "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
                             "  doc_id INTEGER NOT NULL REFERENCES documents(id) ON DELETE CASCADE,"
                             "  chunk_idx INTEGER NOT NULL,"
                             "  content TEXT NOT NULL"
                             ");";

    char sql_vec[512];
    snprintf(sql_vec, sizeof(sql_vec),
             "CREATE VIRTUAL TABLE IF NOT EXISTS vec_chunks USING vec0("
             "  chunk_id INTEGER PRIMARY KEY,"
             "  embedding FLOAT[%d] distance_metric=cosine"
             ");",
             embed_dim);

    char *err = NULL;
    int rc = sqlite3_exec(db->handle, sql_docs, NULL, NULL, &err);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "Error creating documents table: %s\n", err ? err : "unknown");
        if (err)
            sqlite3_free(err);
        return -1;
    }

    rc = sqlite3_exec(db->handle, sql_chunks, NULL, NULL, &err);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "Error creating chunks table: %s\n", err ? err : "unknown");
        if (err)
            sqlite3_free(err);
        return -1;
    }

    rc = sqlite3_exec(db->handle, sql_vec, NULL, NULL, &err);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "Error creating vec_chunks virtual table: %s\n", err ? err : "unknown");
        if (err)
            sqlite3_free(err);
        return -1;
    }

    return 0;
}

int db_begin_transaction(db_context_t *db)
{
    if (!db || !db->handle)
        return -1;
    return sqlite3_exec(db->handle, "BEGIN TRANSACTION;", NULL, NULL, NULL);
}

int db_commit_transaction(db_context_t *db)
{
    if (!db || !db->handle)
        return -1;
    return sqlite3_exec(db->handle, "COMMIT;", NULL, NULL, NULL);
}

int db_rollback_transaction(db_context_t *db)
{
    if (!db || !db->handle)
        return -1;
    return sqlite3_exec(db->handle, "ROLLBACK;", NULL, NULL, NULL);
}

int64_t db_insert_document(db_context_t *db, const char *path)
{
    if (!db || !db->handle || !path)
        return -1;

    const char *sql_find = "SELECT id FROM documents WHERE path = ?;";
    sqlite3_stmt *stmt = NULL;
    if (sqlite3_prepare_v2(db->handle, sql_find, -1, &stmt, NULL) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, path, -1, SQLITE_STATIC);
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            int64_t existing_id = sqlite3_column_int64(stmt, 0);
            sqlite3_finalize(stmt);
            return existing_id;
        }
        sqlite3_finalize(stmt);
    }

    const char *sql_insert = "INSERT INTO documents (path) VALUES (?) RETURNING id;";
    if (sqlite3_prepare_v2(db->handle, sql_insert, -1, &stmt, NULL) != SQLITE_OK) {
        return -1;
    }
    sqlite3_bind_text(stmt, 1, path, -1, SQLITE_STATIC);

    int64_t doc_id = -1;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        doc_id = sqlite3_column_int64(stmt, 0);
    }
    sqlite3_finalize(stmt);
    return doc_id;
}

int db_insert_chunk(db_context_t *db, int64_t doc_id, int chunk_idx, const char *content,
                    const float *vec, int dim)
{
    if (!db || !db->handle || !content || !vec)
        return -1;

    const char *sql_chunk =
        "INSERT INTO chunks (doc_id, chunk_idx, content) VALUES (?, ?, ?) RETURNING id;";
    sqlite3_stmt *stmt_chunk = NULL;
    if (sqlite3_prepare_v2(db->handle, sql_chunk, -1, &stmt_chunk, NULL) != SQLITE_OK) {
        return -1;
    }
    sqlite3_bind_int64(stmt_chunk, 1, doc_id);
    sqlite3_bind_int(stmt_chunk, 2, chunk_idx);
    sqlite3_bind_text(stmt_chunk, 3, content, -1, SQLITE_STATIC);

    int64_t chunk_id = -1;
    if (sqlite3_step(stmt_chunk) == SQLITE_ROW) {
        chunk_id = sqlite3_column_int64(stmt_chunk, 0);
    }
    sqlite3_finalize(stmt_chunk);

    if (chunk_id <= 0) {
        return -1;
    }

    const char *sql_vec = "INSERT INTO vec_chunks (chunk_id, embedding) VALUES (?, ?);";
    sqlite3_stmt *stmt_vec = NULL;
    if (sqlite3_prepare_v2(db->handle, sql_vec, -1, &stmt_vec, NULL) != SQLITE_OK) {
        return -1;
    }
    sqlite3_bind_int64(stmt_vec, 1, chunk_id);
    /* In sqlite-vec, float vectors can be bound as raw float arrays */
    sqlite3_bind_blob(stmt_vec, 2, vec, (int)(sizeof(float) * (size_t)dim), SQLITE_STATIC);

    int rc = sqlite3_step(stmt_vec);
    sqlite3_finalize(stmt_vec);

    return (rc == SQLITE_DONE) ? 0 : -1;
}

int db_search_knn(db_context_t *db, const float *query_vec, int dim, int k,
                  search_result_t **out_results, int *out_count)
{
    if (!db || !db->handle || !query_vec || !out_results || !out_count)
        return -1;
    *out_results = NULL;
    *out_count = 0;

    const char *sql = "SELECT v.chunk_id, v.distance, c.doc_id, d.path, c.content "
                      "FROM vec_chunks v "
                      "JOIN chunks c ON c.id = v.chunk_id "
                      "JOIN documents d ON d.id = c.doc_id "
                      "WHERE v.embedding MATCH ? AND k = ? "
                      "ORDER BY v.distance;";

    sqlite3_stmt *stmt = NULL;
    int rc = sqlite3_prepare_v2(db->handle, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "Failed to prepare KNN search: %s\n", sqlite3_errmsg(db->handle));
        return -1;
    }

    sqlite3_bind_blob(stmt, 1, query_vec, (int)(sizeof(float) * (size_t)dim), SQLITE_STATIC);
    sqlite3_bind_int(stmt, 2, k);

    int capacity = (k > 0) ? k : 4;
    search_result_t *res = malloc(sizeof(search_result_t) * (size_t)capacity);
    if (!res) {
        sqlite3_finalize(stmt);
        return -1;
    }
    int count = 0;

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        if (count >= capacity) {
            capacity *= 2;
            search_result_t *grown = realloc(res, sizeof(search_result_t) * (size_t)capacity);
            if (!grown) {
                db_free_results(res, count);
                sqlite3_finalize(stmt);
                return -1;
            }
            res = grown;
        }

        res[count].chunk_id = sqlite3_column_int64(stmt, 0);
        res[count].distance = (float)sqlite3_column_double(stmt, 1);
        res[count].similarity = 1.0f - res[count].distance;
        res[count].doc_id = sqlite3_column_int64(stmt, 2);

        const char *p = (const char *)sqlite3_column_text(stmt, 3);
        if (p) {
            strncpy(res[count].doc_path, p, sizeof(res[count].doc_path) - 1);
            res[count].doc_path[sizeof(res[count].doc_path) - 1] = '\0';
        } else {
            res[count].doc_path[0] = '\0';
        }

        const char *txt = (const char *)sqlite3_column_text(stmt, 4);
        res[count].content = txt ? strdup(txt) : strdup("");
        count++;
    }

    sqlite3_finalize(stmt);

    *out_results = res;
    *out_count = count;
    return 0;
}

void db_free_results(search_result_t *results, int count)
{
    if (!results)
        return;
    for (int i = 0; i < count; i++) {
        if (results[i].content) {
            free(results[i].content);
        }
    }
    free(results);
}

int db_get_stats(db_context_t *db, int *out_doc_count, int *out_chunk_count)
{
    if (!db || !db->handle)
        return -1;
    if (out_doc_count)
        *out_doc_count = 0;
    if (out_chunk_count)
        *out_chunk_count = 0;

    sqlite3_stmt *stmt = NULL;
    if (sqlite3_prepare_v2(db->handle, "SELECT count(*) FROM documents;", -1, &stmt, NULL) ==
        SQLITE_OK) {
        if (sqlite3_step(stmt) == SQLITE_ROW && out_doc_count) {
            *out_doc_count = sqlite3_column_int(stmt, 0);
        }
        sqlite3_finalize(stmt);
    }

    if (sqlite3_prepare_v2(db->handle, "SELECT count(*) FROM chunks;", -1, &stmt, NULL) ==
        SQLITE_OK) {
        if (sqlite3_step(stmt) == SQLITE_ROW && out_chunk_count) {
            *out_chunk_count = sqlite3_column_int(stmt, 0);
        }
        sqlite3_finalize(stmt);
    }

    return 0;
}
