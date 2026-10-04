#include "pipeline.h"

#include "db.h"
#include "doc.h"
#include "embedder.h"
#include "logger.h"
#include "ui.h"

#include <pthread.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <unistd.h>
#endif

/* -----------------------------------------------------------------------------
 * In-Memory Known Document Hash Set
 * Fast O(1) lock-free lookup for worker threads to skip unchanged/duplicate files
 * -------------------------------------------------------------------------- */

typedef struct doc_cache_node {
    char *path;
    char hash[33];
    struct doc_cache_node *next_path;
    struct doc_cache_node *next_hash;
} doc_cache_node_t;

typedef struct {
    doc_cache_node_t **path_buckets;
    doc_cache_node_t **hash_buckets;
    size_t num_buckets;
    size_t count;
} doc_cache_t;

static void doc_cache_init(doc_cache_t *cache, size_t num_buckets)
{
    cache->num_buckets = num_buckets;
    cache->path_buckets = (doc_cache_node_t **)calloc(num_buckets, sizeof(doc_cache_node_t *));
    cache->hash_buckets = (doc_cache_node_t **)calloc(num_buckets, sizeof(doc_cache_node_t *));
    cache->count = 0;
}

static void doc_cache_populate_cb(const char *path, const char *content_hash, void *user_data)
{
    doc_cache_t *cache = (doc_cache_t *)user_data;
    if (!cache || !cache->path_buckets || !path || !content_hash) {
        return;
    }

    doc_cache_node_t *node = (doc_cache_node_t *)malloc(sizeof(*node));
    if (!node) {
        return;
    }
    node->path = strdup(path);
    if (!node->path) {
        free(node);
        return;
    }
    strncpy(node->hash, content_hash, sizeof(node->hash) - 1);
    node->hash[sizeof(node->hash) - 1] = '\0';

    uint64_t hp = hash_fnv1a64(node->path, strlen(node->path));
    size_t p_idx = (size_t)(hp & (uint64_t)(cache->num_buckets - 1));
    node->next_path = cache->path_buckets[p_idx];
    cache->path_buckets[p_idx] = node;

    uint64_t hh = hash_fnv1a64(node->hash, strlen(node->hash));
    size_t h_idx = (size_t)(hh & (uint64_t)(cache->num_buckets - 1));
    node->next_hash = cache->hash_buckets[h_idx];
    cache->hash_buckets[h_idx] = node;

    cache->count++;
}

static const char *doc_cache_get_hash(const doc_cache_t *cache, const char *path)
{
    if (!cache || !cache->path_buckets || !path) {
        return NULL;
    }
    uint64_t hp = hash_fnv1a64(path, strlen(path));
    size_t p_idx = (size_t)(hp & (uint64_t)(cache->num_buckets - 1));
    for (const doc_cache_node_t *n = cache->path_buckets[p_idx]; n != NULL; n = n->next_path) {
        if (strcmp(n->path, path) == 0) {
            return n->hash;
        }
    }
    return NULL;
}

static bool doc_cache_has_content_hash(const doc_cache_t *cache, const char *content_hash)
{
    if (!cache || !cache->hash_buckets || !content_hash) {
        return false;
    }
    uint64_t hh = hash_fnv1a64(content_hash, strlen(content_hash));
    size_t h_idx = (size_t)(hh & (uint64_t)(cache->num_buckets - 1));
    for (const doc_cache_node_t *n = cache->hash_buckets[h_idx]; n != NULL; n = n->next_hash) {
        if (strcmp(n->hash, content_hash) == 0) {
            return true;
        }
    }
    return false;
}

static void doc_cache_free(doc_cache_t *cache)
{
    if (!cache) {
        return;
    }
    if (cache->path_buckets) {
        for (size_t i = 0; i < cache->num_buckets; i++) {
            doc_cache_node_t *n = cache->path_buckets[i];
            while (n != NULL) {
                doc_cache_node_t *next = n->next_path;
                free(n->path);
                free(n);
                n = next;
            }
        }
        free(cache->path_buckets);
        cache->path_buckets = NULL;
    }
    if (cache->hash_buckets) {
        free(cache->hash_buckets);
        cache->hash_buckets = NULL;
    }
    cache->count = 0;
}

/* -----------------------------------------------------------------------------
 * Thread-Safe Bounded Queue of Extracted Documents
 * -------------------------------------------------------------------------- */

typedef struct {
    char *path;
    char hash_str[33];
    bool is_unchanged;
    bool is_duplicate;
    bool extract_failed;
    chunk_list_t chunks;
} prepared_doc_t;

#define PIPELINE_QUEUE_CAPACITY 32

typedef struct {
    prepared_doc_t items[PIPELINE_QUEUE_CAPACITY];
    size_t head;
    size_t tail;
    size_t count;
    pthread_mutex_t mutex;
    pthread_cond_t not_empty;
    pthread_cond_t not_full;
} doc_queue_t;

static void queue_init(doc_queue_t *q)
{
    q->head = 0;
    q->tail = 0;
    q->count = 0;
    pthread_mutex_init(&q->mutex, NULL);
    pthread_cond_init(&q->not_empty, NULL);
    pthread_cond_init(&q->not_full, NULL);
}

static void queue_push(doc_queue_t *q, prepared_doc_t item)
{
    pthread_mutex_lock(&q->mutex);
    while (q->count == PIPELINE_QUEUE_CAPACITY) {
        pthread_cond_wait(&q->not_full, &q->mutex);
    }
    q->items[q->tail] = item;
    q->tail = (q->tail + 1) % PIPELINE_QUEUE_CAPACITY;
    q->count++;
    pthread_cond_signal(&q->not_empty);
    pthread_mutex_unlock(&q->mutex);
}

static void queue_pop(doc_queue_t *q, prepared_doc_t *out_item)
{
    pthread_mutex_lock(&q->mutex);
    while (q->count == 0) {
        pthread_cond_wait(&q->not_empty, &q->mutex);
    }
    *out_item = q->items[q->head];
    q->head = (q->head + 1) % PIPELINE_QUEUE_CAPACITY;
    q->count--;
    pthread_cond_signal(&q->not_full);
    pthread_mutex_unlock(&q->mutex);
}

static void queue_destroy(doc_queue_t *q)
{
    for (size_t i = 0; i < q->count; i++) {
        size_t idx = (q->head + i) % PIPELINE_QUEUE_CAPACITY;
        free(q->items[idx].path);
        chunk_list_free(&q->items[idx].chunks);
    }
    pthread_mutex_destroy(&q->mutex);
    pthread_cond_destroy(&q->not_empty);
    pthread_cond_destroy(&q->not_full);
}

/* -----------------------------------------------------------------------------
 * Producer Worker Threads
 * -------------------------------------------------------------------------- */

typedef struct {
    char **file_paths;
    size_t file_count;
    size_t next_file_idx;
    pthread_mutex_t file_mutex;
    doc_cache_t *cache;
    doc_queue_t *queue;
    int chunk_size;
    int chunk_overlap;
} pipeline_shared_t;

static void *pipeline_worker_thread(void *arg)
{
    pipeline_shared_t *shared = (pipeline_shared_t *)arg;

    while (1) {
        size_t idx;
        pthread_mutex_lock(&shared->file_mutex);
        idx = shared->next_file_idx++;
        pthread_mutex_unlock(&shared->file_mutex);

        if (idx >= shared->file_count) {
            break;
        }

        const char *file_path = shared->file_paths[idx];
        prepared_doc_t doc;
        memset(&doc, 0, sizeof(doc));
        doc.path = strdup(file_path ? file_path : "");
        if (!doc.path) {
            doc.extract_failed = true;
            queue_push(shared->queue, doc);
            continue;
        }

        /* 1. Fast FNV-1a64 content hash */
        if (!db_hash_file_fnv1a64(file_path, doc.hash_str, sizeof(doc.hash_str))) {
            logger_warn("Could not access file for hashing: %s", file_path);
            doc.extract_failed = true;
            queue_push(shared->queue, doc);
            continue;
        }

        /* 2. Check cache for unchanged path */
        const char *cached_hash = doc_cache_get_hash(shared->cache, file_path);
        if (cached_hash && strcmp(cached_hash, doc.hash_str) == 0) {
            doc.is_unchanged = true;
            queue_push(shared->queue, doc);
            continue;
        }

        /* 3. Check cache for duplicate file content under different path */
        if (!cached_hash && doc_cache_has_content_hash(shared->cache, doc.hash_str)) {
            doc.is_duplicate = true;
            queue_push(shared->queue, doc);
            continue;
        }

        /* 4. Extract text (pdftotext/pandoc/plain) */
        char *content = doc_extract_text(file_path);
        if (!content) {
            doc.extract_failed = true;
            queue_push(shared->queue, doc);
            continue;
        }

        /* 5. Chunk text */
        doc.chunks = chunk_text(content, shared->chunk_size > 0 ? shared->chunk_size : 250,
                                shared->chunk_overlap > 0 ? shared->chunk_overlap : 40);
        free(content);

        if (doc.chunks.count == 0) {
            doc.extract_failed = true;
            queue_push(shared->queue, doc);
            continue;
        }

        /* 6. Push ready document with chunks to bounded queue */
        queue_push(shared->queue, doc);
    }

    return NULL;
}

/* -----------------------------------------------------------------------------
 * Main Pipeline Orchestrator (Consumer & Coordinator)
 * -------------------------------------------------------------------------- */

int pipeline_ingest_files(db_context_t *db, embedder_context_t *emb, char **file_paths,
                          size_t file_count, int embed_dim, int chunk_size, int chunk_overlap,
                          int *total_docs, int *total_chunks, int *total_skipped, int *total_failed)
{
    if (!db || !emb || !file_paths || file_count == 0) {
        return 0;
    }

    /* Step 1: Pre-populate in-memory hash cache from SQLite (takes ~1-2ms) */
    doc_cache_t cache;
    doc_cache_init(&cache, 65536);
    db_for_each_document_hash(db, doc_cache_populate_cb, &cache);

    /* Step 2: Determine worker thread count */
    long nprocs = 4;
#if defined(_WIN32)
    SYSTEM_INFO sysinfo;
    GetSystemInfo(&sysinfo);
    nprocs = (long)sysinfo.dwNumberOfProcessors;
#else
    nprocs = sysconf(_SC_NPROCESSORS_ONLN);
#endif
    if (nprocs < 1) {
        nprocs = 4;
    }
    int num_workers = (int)(nprocs > 4 ? nprocs - 2 : (nprocs > 1 ? nprocs : 1));
    if (num_workers > 8) {
        num_workers = 8;
    }
    if ((size_t)num_workers > file_count) {
        num_workers = (int)file_count;
    }
    if (num_workers < 1) {
        num_workers = 1;
    }

    /* Step 3: Initialize bounded queue and shared worker state */
    doc_queue_t queue;
    queue_init(&queue);

    pipeline_shared_t shared;
    shared.file_paths = file_paths;
    shared.file_count = file_count;
    shared.next_file_idx = 0;
    pthread_mutex_init(&shared.file_mutex, NULL);
    shared.cache = &cache;
    shared.queue = &queue;
    shared.chunk_size = chunk_size;
    shared.chunk_overlap = chunk_overlap;

    pthread_t *threads = (pthread_t *)malloc(sizeof(pthread_t) * (size_t)num_workers);
    if (!threads) {
        doc_cache_free(&cache);
        queue_destroy(&queue);
        pthread_mutex_destroy(&shared.file_mutex);
        return -1;
    }

    for (int i = 0; i < num_workers; i++) {
        pthread_create(&threads[i], NULL, pipeline_worker_thread, &shared);
    }

    /* Step 4: Consumer loop running on main thread */
    db_begin_transaction(db);
    int batch_tx_count = 0;

    for (size_t i = 0; i < file_count; i++) {
        prepared_doc_t doc;
        queue_pop(&queue, &doc);

        ui_ingest_progress(i + 1, file_count, doc.path);

        if (doc.is_unchanged) {
            if (total_skipped) {
                (*total_skipped)++;
            }
        } else if (doc.is_duplicate) {
            db_insert_document_with_hash(db, doc.path, doc.hash_str);
            if (total_skipped) {
                (*total_skipped)++;
            }
        } else if (doc.extract_failed || doc.chunks.count == 0) {
            if (total_failed) {
                (*total_failed)++;
            }
            logger_warn("Could not extract text from: %s", doc.path);
        } else {
            /* Fresh or modified document: check if existing path should have old chunks removed */
            char old_hash[32] = {0};
            bool path_exists =
                db_get_document_hash_by_path(db, doc.path, old_hash, sizeof(old_hash));
            if (path_exists) {
                int64_t old_doc_id = -1;
                if (db_find_document_by_hash(db, old_hash, &old_doc_id) && old_doc_id > 0) {
                    db_delete_document_chunks(db, old_doc_id);
                }
            }

            int64_t dup_doc_id = -1;
            if (!path_exists && db_find_document_by_hash(db, doc.hash_str, &dup_doc_id)) {
                /* Same content already indexed during this session */
                db_insert_document_with_hash(db, doc.path, doc.hash_str);
                if (total_skipped) {
                    (*total_skipped)++;
                }
            } else {
                int64_t doc_id = db_insert_document_with_hash(db, doc.path, doc.hash_str);
                if (doc_id > 0) {
                    float **vecs = (float **)malloc(sizeof(float *) * (size_t)doc.chunks.count);
                    float *vec_pool = (float *)malloc(sizeof(float) * (size_t)doc.chunks.count *
                                                      (size_t)embed_dim);
                    if (vecs && vec_pool) {
                        for (int c = 0; c < doc.chunks.count; c++) {
                            vecs[c] = vec_pool + (size_t)c * (size_t)embed_dim;
                        }

                        int slice_sz = 16;
                        int stored = 0;

                        for (int c_start = 0; c_start < doc.chunks.count; c_start += slice_sz) {
                            int c_count = doc.chunks.count - c_start;
                            if (c_count > slice_sz) {
                                c_count = slice_sz;
                            }

                            ui_ingest_progress_chunk(i + 1, file_count, doc.path, c_start,
                                                     doc.chunks.count);

                            int rc = embedder_embed_batch(
                                emb, (const char *const *)(doc.chunks.chunks + c_start),
                                vecs + c_start, c_count);
                            if (rc != 0) {
                                break;
                            }

                            for (int c = c_start; c < c_start + c_count; c++) {
                                if (db_insert_chunk(db, doc_id, c, doc.chunks.chunks[c], vecs[c],
                                                    embed_dim) == 0) {
                                    stored++;
                                }
                            }

                            ui_ingest_progress_chunk(i + 1, file_count, doc.path, c_start + c_count,
                                                     doc.chunks.count);
                        }

                        if (stored > 0) {
                            if (total_docs) {
                                (*total_docs)++;
                            }
                            if (total_chunks) {
                                (*total_chunks) += stored;
                            }
                        }
                    }
                    free(vecs);
                    free(vec_pool);
                }
            }
        }

        chunk_list_free(&doc.chunks);
        free(doc.path);

        batch_tx_count++;
        if (batch_tx_count >= 10) {
            db_commit_transaction(db);
            db_begin_transaction(db);
            batch_tx_count = 0;
        }
    }

    db_commit_transaction(db);
    ui_clear_status();

    /* Step 5: Join workers and clean up resources */
    for (int i = 0; i < num_workers; i++) {
        pthread_join(threads[i], NULL);
    }
    free(threads);

    pthread_mutex_destroy(&shared.file_mutex);
    queue_destroy(&queue);
    doc_cache_free(&cache);

    return 0;
}
