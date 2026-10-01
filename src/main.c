#include "config.h"
#include "db.h"
#include "embedder.h"
#include "generator.h"
#include "logger.h"
#include "repl.h"
#include "ui.h"

#include <ctype.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#define LIBRARIAN_VERSION "0.1.0"

static double get_time_sec(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec * 1e-9;
}

static char *read_entire_file(const char *path)
{
    FILE *fp = fopen(path, "rb");
    if (!fp)
        return NULL;

    if (fseek(fp, 0, SEEK_END) != 0) {
        fclose(fp);
        return NULL;
    }
    long sz = ftell(fp);
    if (sz < 0) {
        fclose(fp);
        return NULL;
    }
    if (fseek(fp, 0, SEEK_SET) != 0) {
        fclose(fp);
        return NULL;
    }

    char *buf = malloc((size_t)sz + 1);
    if (!buf) {
        fclose(fp);
        return NULL;
    }

    size_t read_bytes = fread(buf, 1, (size_t)sz, fp);
    buf[read_bytes] = '\0';
    fclose(fp);
    return buf;
}

static int ingest_single_file(db_context_t *db, embedder_context_t *emb, const char *file_path,
                              int dim)
{
    char *content = read_entire_file(file_path);
    if (!content) {
        logger_warn("Could not read file: %s", file_path);
        return -1;
    }

    chunk_list_t chunks = chunk_text(content, 120, 20);
    free(content);

    if (chunks.count == 0) {
        chunk_list_free(&chunks);
        return 0;
    }

    if (db_begin_transaction(db) != 0) {
        chunk_list_free(&chunks);
        return -1;
    }

    int64_t doc_id = db_insert_document(db, file_path);
    if (doc_id < 0) {
        db_rollback_transaction(db);
        chunk_list_free(&chunks);
        return -1;
    }

    float *vec = malloc(sizeof(float) * (size_t)dim);
    if (!vec) {
        db_rollback_transaction(db);
        chunk_list_free(&chunks);
        return -1;
    }

    int stored = 0;
    for (int i = 0; i < chunks.count; i++) {
        if (embedder_embed(emb, chunks.chunks[i], vec) == 0) {
            if (db_insert_chunk(db, doc_id, i, chunks.chunks[i], vec, dim) == 0) {
                stored++;
            }
        }
    }

    free(vec);
    db_commit_transaction(db);
    chunk_list_free(&chunks);
    return stored;
}

typedef struct {
    char **paths;
    size_t count;
    size_t cap;
} file_list_t;

static void file_list_init(file_list_t *list)
{
    list->paths = NULL;
    list->count = 0;
    list->cap = 0;
}

static void file_list_add(file_list_t *list, const char *path)
{
    if (list->count >= list->cap) {
        size_t ncap = list->cap == 0 ? 32 : list->cap * 2;
        char **grown = (char **)realloc(list->paths, ncap * sizeof(char *));
        if (!grown) {
            return;
        }
        list->paths = grown;
        list->cap = ncap;
    }
    char *p = strdup(path);
    if (p) {
        list->paths[list->count++] = p;
    }
}

static void file_list_free(file_list_t *list)
{
    if (!list) {
        return;
    }
    for (size_t i = 0; i < list->count; i++) {
        free(list->paths[i]);
    }
    free(list->paths);
    list->paths = NULL;
    list->count = 0;
    list->cap = 0;
}

static void collect_files_recursive(const char *path, file_list_t *list)
{
    struct stat st;
    if (stat(path, &st) != 0) {
        return;
    }

    if (S_ISREG(st.st_mode)) {
        file_list_add(list, path);
        return;
    }

    if (S_ISDIR(st.st_mode)) {
        DIR *dir = opendir(path);
        if (!dir) {
            return;
        }

        const struct dirent *entry;
        while ((entry = readdir(dir)) != NULL) {
            if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
                continue;
            }
            if (entry->d_name[0] == '.') {
                continue; /* skip hidden files/dirs */
            }

            char subpath[2048];
            snprintf(subpath, sizeof(subpath), "%s/%s", path, entry->d_name);
            collect_files_recursive(subpath, list);
        }
        closedir(dir);
    }
}

static void expand_user_path(const char *in, char *out, size_t out_sz)
{
    if (in[0] == '~' && (in[1] == '/' || in[1] == '\0')) {
        const char *home = getenv("HOME");
        if (home) {
            snprintf(out, out_sz, "%s%s", home, in + 1);
            return;
        }
    }
    strncpy(out, in, out_sz - 1);
    out[out_sz - 1] = '\0';
}

static int ingest_path(db_context_t *db, embedder_context_t *emb, const char *raw_path, int dim,
                       int *total_docs, int *total_chunks)
{
    char path[2048];
    expand_user_path(raw_path, path, sizeof(path));

    struct stat st;
    if (stat(path, &st) != 0) {
        ui_error("Path does not exist: %s", raw_path);
        return -1;
    }

    file_list_t files;
    file_list_init(&files);
    collect_files_recursive(path, &files);

    if (files.count == 0) {
        ui_warn("No regular files found to ingest in: %s", raw_path);
        file_list_free(&files);
        return 0;
    }

    for (size_t i = 0; i < files.count; i++) {
        ui_ingest_progress(i + 1, files.count, files.paths[i]);
        int n_chunks = ingest_single_file(db, emb, files.paths[i], dim);
        if (n_chunks >= 0) {
            (*total_docs)++;
            (*total_chunks) += n_chunks;
        }
    }

    ui_clear_status();
    file_list_free(&files);
    return 0;
}

static void run_query_core(db_context_t *db, embedder_context_t *emb, generator_context_t *gen,
                           const librarian_config_t *cfg, const char *question)
{
    double t_start = get_time_sec();
    ui_status("🔍 [searching vector index...]");

    float *qvec = malloc(sizeof(float) * (size_t)cfg->embed_dimension);
    if (!qvec) {
        ui_clear_status();
        ui_error("Out of memory for query vector");
        return;
    }

    if (embedder_embed(emb, question, qvec) != 0) {
        ui_clear_status();
        free(qvec);
        ui_error("Failed to compute embedding for question");
        return;
    }

    search_result_t *results = NULL;
    int count = 0;
    int rc = db_search_knn(db, qvec, cfg->embed_dimension, 4, &results, &count);
    free(qvec);
    double t_search = get_time_sec();

    if (rc != 0 || count == 0) {
        ui_clear_status();
        ui_similarity_badge(0.0f, cfg->similarity_threshold);
        ui_confidence_badge(0.0f, true);
        printf("\n" COLOR_PEACH
               "Stage 1 Refusal: No matching documents found in knowledge base." COLOR_RESET "\n");
        printf(COLOR_GRAY "Ingest reference documents using `/ingest <path>`." COLOR_RESET "\n\n");
        if (results)
            db_free_results(results, count);
        return;
    }

    /* Check Stage 1 Retrieval Refusal */
    if (results[0].similarity < cfg->similarity_threshold) {
        ui_clear_status();
        ui_similarity_badge(results[0].similarity, cfg->similarity_threshold);
        ui_confidence_badge(results[0].similarity, true);
        printf(
            "\n" COLOR_PEACH
            "Stage 1 Refusal: Best match similarity (%.3f) is below threshold (%.3f)." COLOR_RESET
            "\n",
            (double)results[0].similarity, (double)cfg->similarity_threshold);
        printf(COLOR_GRAY
               "I do not have sufficient relevant documents to answer this question." COLOR_RESET
               "\n\n");
        db_free_results(results, count);
        return;
    }

    /* Stage 1 passed -> Run Generator */
    ui_status("🧠 [generating response...]");

    generation_result_t gen_res = generator_generate(
        gen, question, results, count, cfg->similarity_threshold, cfg->confidence_threshold);
    double t_end = get_time_sec();
    ui_clear_status();

    ui_similarity_badge(results[0].similarity, cfg->similarity_threshold);
    ui_confidence_badge(gen_res.confidence, gen_res.is_refusal);

    if (gen_res.is_refusal) {
        printf("\n" COLOR_PEACH "%s" COLOR_RESET "\n", gen_res.refusal_reason);
        printf(COLOR_GRAY "%s" COLOR_RESET "\n\n", gen_res.text ? gen_res.text : "");
    } else {
        printf("\n" COLOR_MINT "%s" COLOR_RESET "\n\n", gen_res.text ? gen_res.text : "");
    }

    printf(COLOR_GRAY "⏱ Search: %.2fs • Generation: %.2fs • Total: %.2fs\n" COLOR_RESET "\n",
           t_search - t_start, t_end - t_search, t_end - t_start);

    generation_result_free(&gen_res);
    db_free_results(results, count);
}

static int run_ingest_mode(const librarian_config_t *cfg, const char *path)
{
    ui_info("Initializing database: %s", cfg->db_path);
    db_context_t *db = db_open(cfg->db_path);
    if (!db)
        return 1;

    db_init_schema(db, cfg->embed_dimension);

    ui_info("Loading embedding model: %s", cfg->embed_model_path);
    embedder_context_t *emb = embedder_init(cfg->embed_model_path, cfg->embed_dimension);
    if (!emb) {
        db_close(db);
        return 1;
    }

    ui_info("Ingesting path: %s", path);
    double t_start = get_time_sec();
    int docs = 0, chunks = 0;
    ingest_path(db, emb, path, cfg->embed_dimension, &docs, &chunks);
    double t_end = get_time_sec();

    ui_success("Ingestion complete: %d documents, %d total chunks stored (⏱ %.2fs)", docs, chunks,
               t_end - t_start);

    embedder_free(emb);
    db_close(db);
    return 0;
}

static int run_query_mode(const librarian_config_t *cfg, const char *question)
{
    db_context_t *db = db_open(cfg->db_path);
    if (!db) {
        ui_error("Failed to open database %s", cfg->db_path);
        return 1;
    }

    embedder_context_t *emb = embedder_init(cfg->embed_model_path, cfg->embed_dimension);
    if (!emb) {
        db_close(db);
        return 1;
    }

    printf("\n" COLOR_BOLD "Question: " COLOR_RESET "%s\n", question);
    ui_status("🔍 [searching vector index...]");

    double t_start = get_time_sec();
    float *qvec = malloc(sizeof(float) * (size_t)cfg->embed_dimension);
    if (!qvec) {
        ui_clear_status();
        embedder_free(emb);
        db_close(db);
        ui_error("Out of memory for query vector");
        return 1;
    }

    if (embedder_embed(emb, question, qvec) != 0) {
        ui_clear_status();
        free(qvec);
        embedder_free(emb);
        db_close(db);
        ui_error("Failed to compute embedding for question");
        return 1;
    }

    search_result_t *results = NULL;
    int count = 0;
    int rc = db_search_knn(db, qvec, cfg->embed_dimension, 4, &results, &count);
    free(qvec);
    double t_search = get_time_sec();

    if (rc != 0 || count == 0) {
        ui_clear_status();
        ui_similarity_badge(0.0f, cfg->similarity_threshold);
        ui_confidence_badge(0.0f, true);
        printf("\n" COLOR_PEACH
               "Stage 1 Refusal: No matching documents found in knowledge base." COLOR_RESET "\n");
        printf(COLOR_GRAY
               "Please ingest reference documents using `librarian ingest <path>`." COLOR_RESET
               "\n\n");
        if (results)
            db_free_results(results, count);
        embedder_free(emb);
        db_close(db);
        return 0;
    }

    /* Check Stage 1 Retrieval Refusal */
    if (results[0].similarity < cfg->similarity_threshold) {
        ui_clear_status();
        ui_similarity_badge(results[0].similarity, cfg->similarity_threshold);
        ui_confidence_badge(results[0].similarity, true);
        printf(
            "\n" COLOR_PEACH
            "Stage 1 Refusal: Best match similarity (%.3f) is below threshold (%.3f)." COLOR_RESET
            "\n",
            (double)results[0].similarity, (double)cfg->similarity_threshold);
        printf(COLOR_GRAY
               "I do not have sufficient relevant documents to answer this question." COLOR_RESET
               "\n\n");
        printf(COLOR_GRAY "⏱ Search: %.2fs\n" COLOR_RESET "\n", t_search - t_start);
        db_free_results(results, count);
        embedder_free(emb);
        db_close(db);
        return 0;
    }

    /* Stage 1 passed! Lazily load generator */
    ui_status("🧠 [initializing generator...]");
    generator_context_t *gen = generator_init(cfg->gen_model_path, cfg->gen_context_length);
    if (!gen) {
        ui_clear_status();
        db_free_results(results, count);
        embedder_free(emb);
        db_close(db);
        return 1;
    }

    ui_status("🧠 [generating response...]");
    generation_result_t gen_res = generator_generate(
        gen, question, results, count, cfg->similarity_threshold, cfg->confidence_threshold);
    double t_end = get_time_sec();
    ui_clear_status();

    ui_similarity_badge(results[0].similarity, cfg->similarity_threshold);
    ui_confidence_badge(gen_res.confidence, gen_res.is_refusal);

    if (gen_res.is_refusal) {
        printf("\n" COLOR_PEACH "%s" COLOR_RESET "\n", gen_res.refusal_reason);
        printf(COLOR_GRAY "%s" COLOR_RESET "\n\n", gen_res.text ? gen_res.text : "");
    } else {
        printf("\n" COLOR_MINT "%s" COLOR_RESET "\n\n", gen_res.text ? gen_res.text : "");
    }

    printf(COLOR_GRAY "⏱ Search: %.2fs • Generation: %.2fs • Total: %.2fs\n" COLOR_RESET "\n",
           t_search - t_start, t_end - t_search, t_end - t_start);

    generation_result_free(&gen_res);
    db_free_results(results, count);
    generator_free(gen);
    embedder_free(emb);
    db_close(db);
    return 0;
}

static void show_repl_help(void)
{
    printf("\n" COLOR_LAVENDER COLOR_BOLD "📚 Librarian REPL Commands:" COLOR_RESET "\n");
    printf("  " COLOR_MINT "/help" COLOR_RESET "            Display this interactive help menu\n");
    printf("  " COLOR_MINT "/ingest <path>" COLOR_RESET
           "   Ingest a text file or directory recursively\n");
    printf("  " COLOR_MINT "/stats" COLOR_RESET
           "           Display vector database document and chunk statistics\n");
    printf("  " COLOR_MINT "/config" COLOR_RESET
           "          Display current model thresholds and paths\n");
    printf("  " COLOR_MINT "/clear" COLOR_RESET
           "           Clear screen and display welcome banner\n");
    printf("  " COLOR_MINT "/exit" COLOR_RESET ", " COLOR_MINT "/quit" COLOR_RESET
           "      Exit the interactive session\n");
    printf(COLOR_GRAY
           "  Enter any query to search documents and generate a verified answer.\n" COLOR_RESET
           "\n");
}

static int run_chat_mode(const librarian_config_t *cfg)
{
    ui_banner();

    ui_info("Opening vector database: %s", cfg->db_path);
    db_context_t *db = db_open(cfg->db_path);
    if (!db) {
        ui_error("Failed to open database");
        return 1;
    }
    db_init_schema(db, cfg->embed_dimension);

    ui_info("Loading embedder: %s", cfg->embed_model_path);
    embedder_context_t *emb = embedder_init(cfg->embed_model_path, cfg->embed_dimension);
    if (!emb) {
        db_close(db);
        return 1;
    }

    ui_info("Loading generator: %s", cfg->gen_model_path);
    generator_context_t *gen = generator_init(cfg->gen_model_path, cfg->gen_context_length);
    if (!gen) {
        embedder_free(emb);
        db_close(db);
        return 1;
    }

    ui_success("Models loaded into memory. Interactive REPL active.");
    printf(COLOR_GRAY "Type /help for commands, or type your question directly.\n" COLOR_RESET
                      "\n");

    /* Initialize interactive REPL with persistent history */
    char hist_path[512];
    snprintf(hist_path, sizeof(hist_path), "data/history.txt");
    repl_context_t *repl = repl_init(hist_path);

    const char *prompt_str = COLOR_LAVENDER COLOR_BOLD "📚 librarian" COLOR_MINT " ❯ " COLOR_RESET;

    while (1) {
        char *line = repl_readline(repl, prompt_str);
        if (!line) {
            printf("\n");
            ui_info("Session ended. Goodbye!");
            break;
        }

        /* Skip leading whitespace */
        char *cmd = line;
        while (*cmd && isspace((unsigned char)*cmd))
            cmd++;
        if (*cmd == '\0')
            continue;

        /* Add to history */
        repl_history_add(repl, cmd);

        if (strcmp(cmd, "/exit") == 0 || strcmp(cmd, "/quit") == 0 || strcmp(cmd, "/q") == 0) {
            ui_info("Shutting down Librarian. Goodbye!");
            break;
        }

        if (strcmp(cmd, "/help") == 0 || strcmp(cmd, "/?") == 0) {
            show_repl_help();
            continue;
        }

        if (strcmp(cmd, "/clear") == 0) {
            printf("\x1b[2J\x1b[H");
            ui_banner();
            continue;
        }

        if (strcmp(cmd, "/config") == 0) {
            config_print(cfg);
            continue;
        }

        if (strcmp(cmd, "/stats") == 0) {
            int doc_count = 0, chunk_count = 0;
            db_get_stats(db, &doc_count, &chunk_count);
            printf("\n" COLOR_LAVENDER COLOR_BOLD "Database Statistics:" COLOR_RESET "\n");
            printf("  • Path: %s\n", cfg->db_path);
            printf("  • Documents: %d\n", doc_count);
            printf("  • Chunks: %d\n", chunk_count);
            printf("  • Vector Dimension: %d\n\n", cfg->embed_dimension);
            continue;
        }

        if (strncmp(cmd, "/ingest ", 8) == 0) {
            char *target = cmd + 8;
            while (*target && isspace((unsigned char)*target))
                target++;
            if (*target) {
                ui_info("Ingesting: %s", target);
                double t0 = get_time_sec();
                int docs = 0, chunks = 0;
                ingest_path(db, emb, target, cfg->embed_dimension, &docs, &chunks);
                double t1 = get_time_sec();
                ui_success("Ingested %d documents, %d total chunks (⏱ %.2fs)", docs, chunks,
                           t1 - t0);
            } else {
                ui_warn("Usage: /ingest <path>");
            }
            continue;
        }

        if (cmd[0] == '/') {
            ui_warn("Unknown command '%s'. Type /help for available commands.", cmd);
            continue;
        }

        /* Normal user question query */
        run_query_core(db, emb, gen, cfg, cmd);
    }

    repl_free(repl);
    generator_free(gen);
    embedder_free(emb);
    db_close(db);
    return 0;
}

static void print_usage(const char *prog)
{
    printf(COLOR_LAVENDER COLOR_BOLD "librarian" COLOR_RESET
                                     " - Embedded Pure C99 RAG System v%s\n\n",
           LIBRARIAN_VERSION);
    printf(COLOR_BOLD "USAGE:" COLOR_RESET "\n");
    printf("  %s ingest <file_or_dir>   Index documents into local vector database\n", prog);
    printf("  %s query \"<question>\"     Execute one-off retrieval + generation query\n", prog);
    printf("  %s chat                   Start interactive REPL session\n", prog);
    printf("  %s --version              Display version information\n", prog);
    printf("  %s --help                 Display this help menu\n\n", prog);
    printf(COLOR_BOLD "OPTIONS:" COLOR_RESET "\n");
    printf("  -h, --help                Show help instructions\n");
    printf("  -v, --version             Show version and build details\n\n");
}

int main(int argc, char **argv)
{
    if (argc < 2) {
        print_usage(argv[0]);
        return 0;
    }

    if (strcmp(argv[1], "--version") == 0 || strcmp(argv[1], "-v") == 0) {
        printf("librarian version %s (C99, sqlite-vec, llama.cpp)\n", LIBRARIAN_VERSION);
        return 0;
    }

    if (strcmp(argv[1], "--help") == 0 || strcmp(argv[1], "-h") == 0) {
        print_usage(argv[0]);
        return 0;
    }

    librarian_config_t cfg;
    if (config_load(CONFIG_DEFAULT_PATH, &cfg) != 0) {
        ui_warn("Could not load %s; using default configuration", CONFIG_DEFAULT_PATH);
    }

    /* Initialize logger to redirect llama.cpp/ggml output to log file */
    if (logger_init(cfg.log_path) != 0) {
        ui_warn("Could not open log file %s", cfg.log_path);
    }

    int rc = 0;
    if (strcmp(argv[1], "ingest") == 0) {
        if (argc < 3) {
            ui_error("Missing path to ingest. Usage: %s ingest <file_or_dir>", argv[0]);
            logger_close();
            return 1;
        }
        rc = run_ingest_mode(&cfg, argv[2]);
    } else if (strcmp(argv[1], "query") == 0) {
        if (argc < 3) {
            ui_error("Missing query string. Usage: %s query \"<question>\"", argv[0]);
            logger_close();
            return 1;
        }
        rc = run_query_mode(&cfg, argv[2]);
    } else if (strcmp(argv[1], "chat") == 0) {
        rc = run_chat_mode(&cfg);
    } else {
        ui_error("Unknown command '%s'. Run '%s --help' for usage.", argv[1], argv[0]);
        rc = 1;
    }

    logger_close();
    return rc;
}
