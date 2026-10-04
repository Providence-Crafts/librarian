#include "db.h"

#include "sqlite-vec.h"
#include "sqlite3.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>

#if defined(_WIN32)
#include <direct.h>
#define mkdir_portable(p) _mkdir(p)
#else
#define mkdir_portable(p) mkdir(p, 0755)
#endif

struct db_context {
    sqlite3 *handle;
    int embed_dim;
};

static void ensure_dir_exists(const char *file_path)
{
    char tmp[512];
    strncpy(tmp, file_path, sizeof(tmp) - 1);
    tmp[sizeof(tmp) - 1] = '\0';
    char *slash = strrchr(tmp, '/');
#if defined(_WIN32)
    if (!slash)
        slash = strrchr(tmp, '\\');
#endif
    if (slash) {
        *slash = '\0';
        if (tmp[0] != '\0' && strcmp(tmp, ".") != 0) {
            (void)mkdir_portable(tmp);
        }
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
                           "  content_hash TEXT,"
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

    /* Migrate older schema if content_hash column does not exist yet */
    sqlite3_exec(db->handle, "ALTER TABLE documents ADD COLUMN content_hash TEXT;", NULL, NULL,
                 NULL);
    sqlite3_exec(db->handle, "CREATE INDEX IF NOT EXISTS idx_docs_hash ON documents(content_hash);",
                 NULL, NULL, NULL);

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

int db_reset(db_context_t *db, int embed_dim)
{
    if (!db || !db->handle)
        return -1;

    char *err = NULL;
    const char *sql_reset = "DROP TABLE IF EXISTS vec_chunks;\n"
                            "DROP TABLE IF EXISTS chunks;\n"
                            "DROP TABLE IF EXISTS documents;\n";

    if (sqlite3_exec(db->handle, sql_reset, NULL, NULL, &err) != SQLITE_OK) {
        if (err) {
            fprintf(stderr, "Error resetting database tables: %s\n", err);
            sqlite3_free(err);
        }
        return -1;
    }

    if (db_init_schema(db, embed_dim) != 0) {
        return -1;
    }

    sqlite3_exec(db->handle, "VACUUM;", NULL, NULL, NULL);
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

uint64_t hash_fnv1a64(const void *data, size_t len)
{
    const unsigned char *p = (const unsigned char *)data;
    uint64_t hash = 14695981039346656037ULL;
    for (size_t i = 0; i < len; i++) {
        hash ^= (uint64_t)p[i];
        hash *= 1099511628211ULL;
    }
    return hash;
}

bool db_hash_file_fnv1a64(const char *path, char *out_hash, size_t out_hash_sz)
{
    if (!path || !out_hash || out_hash_sz < 17) {
        return false;
    }

    FILE *fp = fopen(path, "rb");
    if (!fp) {
        return false;
    }

    uint64_t hash = 14695981039346656037ULL;
    unsigned char buf[65536];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), fp)) > 0) {
        for (size_t i = 0; i < n; i++) {
            hash ^= (uint64_t)buf[i];
            hash *= 1099511628211ULL;
        }
    }
    fclose(fp);

    snprintf(out_hash, out_hash_sz, "%016llx", (unsigned long long)hash);
    return true;
}

int64_t db_insert_document_with_hash(db_context_t *db, const char *path, const char *content_hash)
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

            /* Update content_hash if provided */
            if (content_hash) {
                const char *sql_upd = "UPDATE documents SET content_hash = ? WHERE id = ?;";
                sqlite3_stmt *upd_stmt = NULL;
                if (sqlite3_prepare_v2(db->handle, sql_upd, -1, &upd_stmt, NULL) == SQLITE_OK) {
                    sqlite3_bind_text(upd_stmt, 1, content_hash, -1, SQLITE_STATIC);
                    sqlite3_bind_int64(upd_stmt, 2, existing_id);
                    sqlite3_step(upd_stmt);
                    sqlite3_finalize(upd_stmt);
                }
            }
            return existing_id;
        }
        sqlite3_finalize(stmt);
    }

    const char *sql_insert =
        "INSERT INTO documents (path, content_hash) VALUES (?, ?) RETURNING id;";
    if (sqlite3_prepare_v2(db->handle, sql_insert, -1, &stmt, NULL) != SQLITE_OK) {
        return -1;
    }
    sqlite3_bind_text(stmt, 1, path, -1, SQLITE_STATIC);
    if (content_hash) {
        sqlite3_bind_text(stmt, 2, content_hash, -1, SQLITE_STATIC);
    } else {
        sqlite3_bind_null(stmt, 2);
    }

    int64_t doc_id = -1;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        doc_id = sqlite3_column_int64(stmt, 0);
    }
    sqlite3_finalize(stmt);
    return doc_id;
}

int64_t db_insert_document(db_context_t *db, const char *path)
{
    return db_insert_document_with_hash(db, path, NULL);
}

bool db_find_document_by_hash(db_context_t *db, const char *content_hash, int64_t *out_doc_id)
{
    if (!db || !db->handle || !content_hash)
        return false;

    const char *sql = "SELECT id FROM documents WHERE content_hash = ? LIMIT 1;";
    sqlite3_stmt *stmt = NULL;
    if (sqlite3_prepare_v2(db->handle, sql, -1, &stmt, NULL) != SQLITE_OK) {
        return false;
    }

    sqlite3_bind_text(stmt, 1, content_hash, -1, SQLITE_STATIC);
    bool found = false;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        if (out_doc_id) {
            *out_doc_id = sqlite3_column_int64(stmt, 0);
        }
        found = true;
    }
    sqlite3_finalize(stmt);
    return found;
}

bool db_get_document_hash_by_path(db_context_t *db, const char *path, char *out_hash,
                                  size_t out_hash_sz)
{
    if (!db || !db->handle || !path || !out_hash || out_hash_sz == 0)
        return false;

    const char *sql = "SELECT content_hash FROM documents WHERE path = ? LIMIT 1;";
    sqlite3_stmt *stmt = NULL;
    if (sqlite3_prepare_v2(db->handle, sql, -1, &stmt, NULL) != SQLITE_OK) {
        return false;
    }

    sqlite3_bind_text(stmt, 1, path, -1, SQLITE_STATIC);
    bool found = false;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        const char *h = (const char *)sqlite3_column_text(stmt, 0);
        if (h && h[0] != '\0') {
            strncpy(out_hash, h, out_hash_sz - 1);
            out_hash[out_hash_sz - 1] = '\0';
            found = true;
        }
    }
    sqlite3_finalize(stmt);
    return found;
}

int db_for_each_document_hash(db_context_t *db, db_doc_hash_cb callback, void *user_data)
{
    if (!db || !db->handle || !callback)
        return -1;

    const char *sql = "SELECT path, content_hash FROM documents WHERE content_hash IS NOT NULL;";
    sqlite3_stmt *stmt = NULL;
    if (sqlite3_prepare_v2(db->handle, sql, -1, &stmt, NULL) != SQLITE_OK)
        return -1;

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        const char *p = (const char *)sqlite3_column_text(stmt, 0);
        const char *h = (const char *)sqlite3_column_text(stmt, 1);
        if (p && h) {
            callback(p, h, user_data);
        }
    }
    sqlite3_finalize(stmt);
    return 0;
}

int db_delete_document_chunks(db_context_t *db, int64_t doc_id)
{
    if (!db || !db->handle || doc_id <= 0)
        return -1;

    const char *sql_del_vec =
        "DELETE FROM vec_chunks WHERE chunk_id IN (SELECT id FROM chunks WHERE doc_id = ?);";
    sqlite3_stmt *stmt = NULL;
    if (sqlite3_prepare_v2(db->handle, sql_del_vec, -1, &stmt, NULL) == SQLITE_OK) {
        sqlite3_bind_int64(stmt, 1, doc_id);
        sqlite3_step(stmt);
        sqlite3_finalize(stmt);
    }

    const char *sql_del_chunks = "DELETE FROM chunks WHERE doc_id = ?;";
    if (sqlite3_prepare_v2(db->handle, sql_del_chunks, -1, &stmt, NULL) == SQLITE_OK) {
        sqlite3_bind_int64(stmt, 1, doc_id);
        sqlite3_step(stmt);
        sqlite3_finalize(stmt);
    }

    return 0;
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

    const char *sql = "SELECT v.chunk_id, v.distance, c.doc_id, d.path, c.content, c.chunk_idx "
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
        res[count].chunk_idx = sqlite3_column_int(stmt, 5);
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

int db_list_documents(db_context_t *db, const char *search_pattern, doc_info_t **out_docs,
                      int *out_count)
{
    if (!db || !db->handle || !out_docs || !out_count) {
        return -1;
    }
    *out_docs = NULL;
    *out_count = 0;

    bool has_filter = (search_pattern && search_pattern[0] != '\0');
    const char *sql = has_filter ? "SELECT d.id, d.path, COALESCE(d.content_hash, ''), COUNT(c.id) "
                                   "FROM documents d "
                                   "LEFT JOIN chunks c ON c.doc_id = d.id "
                                   "WHERE d.path LIKE ? "
                                   "GROUP BY d.id, d.path, d.content_hash "
                                   "ORDER BY d.id ASC;"
                                 : "SELECT d.id, d.path, COALESCE(d.content_hash, ''), COUNT(c.id) "
                                   "FROM documents d "
                                   "LEFT JOIN chunks c ON c.doc_id = d.id "
                                   "GROUP BY d.id, d.path, d.content_hash "
                                   "ORDER BY d.id ASC;";

    sqlite3_stmt *stmt = NULL;
    if (sqlite3_prepare_v2(db->handle, sql, -1, &stmt, NULL) != SQLITE_OK) {
        return -1;
    }

    if (has_filter) {
        char pattern_buf[512];
        snprintf(pattern_buf, sizeof(pattern_buf), "%%%s%%", search_pattern);
        sqlite3_bind_text(stmt, 1, pattern_buf, -1, SQLITE_TRANSIENT);
    }

    int capacity = 16;
    doc_info_t *docs = malloc(sizeof(doc_info_t) * (size_t)capacity);
    if (!docs) {
        sqlite3_finalize(stmt);
        return -1;
    }
    int count = 0;

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        if (count >= capacity) {
            capacity *= 2;
            doc_info_t *grown = realloc(docs, sizeof(doc_info_t) * (size_t)capacity);
            if (!grown) {
                free(docs);
                sqlite3_finalize(stmt);
                return -1;
            }
            docs = grown;
        }

        docs[count].id = sqlite3_column_int64(stmt, 0);
        const char *p = (const char *)sqlite3_column_text(stmt, 1);
        if (p) {
            strncpy(docs[count].path, p, sizeof(docs[count].path) - 1);
            docs[count].path[sizeof(docs[count].path) - 1] = '\0';
        } else {
            docs[count].path[0] = '\0';
        }

        const char *h = (const char *)sqlite3_column_text(stmt, 2);
        if (h) {
            strncpy(docs[count].content_hash, h, sizeof(docs[count].content_hash) - 1);
            docs[count].content_hash[sizeof(docs[count].content_hash) - 1] = '\0';
        } else {
            docs[count].content_hash[0] = '\0';
        }

        docs[count].chunk_count = sqlite3_column_int(stmt, 3);
        count++;
    }
    sqlite3_finalize(stmt);

    *out_docs = docs;
    *out_count = count;
    return 0;
}

void db_free_doc_info(doc_info_t *docs, int count)
{
    (void)count;
    free(docs);
}

static int count_words(const char *text)
{
    if (!text) {
        return 0;
    }
    int count = 0;
    bool in_word = false;
    for (size_t i = 0; text[i]; i++) {
        if (isspace((unsigned char)text[i])) {
            in_word = false;
        } else if (!in_word) {
            in_word = true;
            count++;
        }
    }
    return count;
}

int db_get_document_chunks(db_context_t *db, int64_t doc_id, chunk_info_t **out_chunks,
                           int *out_count)
{
    if (!db || !db->handle || !out_chunks || !out_count) {
        return -1;
    }
    *out_chunks = NULL;
    *out_count = 0;

    const char *sql =
        "SELECT id, chunk_idx, content FROM chunks WHERE doc_id = ? ORDER BY chunk_idx ASC;";
    sqlite3_stmt *stmt = NULL;
    if (sqlite3_prepare_v2(db->handle, sql, -1, &stmt, NULL) != SQLITE_OK) {
        return -1;
    }
    sqlite3_bind_int64(stmt, 1, doc_id);

    int capacity = 16;
    chunk_info_t *chunks = malloc(sizeof(chunk_info_t) * (size_t)capacity);
    if (!chunks) {
        sqlite3_finalize(stmt);
        return -1;
    }
    int count = 0;

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        if (count >= capacity) {
            capacity *= 2;
            chunk_info_t *grown = realloc(chunks, sizeof(chunk_info_t) * (size_t)capacity);
            if (!grown) {
                db_free_chunk_info(chunks, count);
                sqlite3_finalize(stmt);
                return -1;
            }
            chunks = grown;
        }

        chunks[count].id = sqlite3_column_int64(stmt, 0);
        chunks[count].chunk_idx = sqlite3_column_int(stmt, 1);
        const char *txt = (const char *)sqlite3_column_text(stmt, 2);
        chunks[count].content = txt ? strdup(txt) : strdup("");
        chunks[count].word_count = count_words(chunks[count].content);
        count++;
    }
    sqlite3_finalize(stmt);

    *out_chunks = chunks;
    *out_count = count;
    return 0;
}

void db_free_chunk_info(chunk_info_t *chunks, int count)
{
    if (!chunks) {
        return;
    }
    for (int i = 0; i < count; i++) {
        free(chunks[i].content);
    }
    free(chunks);
}
