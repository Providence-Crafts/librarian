#include "config.h"
#include "db.h"
#include "doc.h"
#include "embedder.h"
#include "generator.h"
#include "logger.h"
#include "pipeline.h"
#include "repl.h"
#include "ui.h"

#include <ctype.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <io.h>
#include <windows.h>
#define isatty _isatty
#define fileno _fileno
#else
#include <unistd.h>
#endif

#define LIBRARIAN_VERSION "0.1.0"

static double get_time_sec(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec * 1e-9;
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
        const char *ext = strrchr(path, '.');
        if (!ext || !doc_is_supported_extension(ext + 1)) {
            return;
        }
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
            if (strcmp(entry->d_name, "__pycache__") == 0 ||
                strcmp(entry->d_name, "node_modules") == 0) {
                continue;
            }

            char subpath[2048];
            snprintf(subpath, sizeof(subpath), "%s/%s", path, entry->d_name);
            collect_files_recursive(subpath, list);
        }
        closedir(dir);
    }
}

static int ingest_path(db_context_t *db, embedder_context_t *emb, const char *raw_path, int dim,
                       int chunk_size, int chunk_overlap, int *total_docs, int *total_chunks,
                       int *total_skipped, int *total_failed)
{
    char path[2048];
    doc_clean_path(raw_path, path, sizeof(path));

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

    int rc =
        pipeline_ingest_files(db, emb, files.paths, files.count, dim, chunk_size, chunk_overlap,
                              total_docs, total_chunks, total_skipped, total_failed);
    file_list_free(&files);
    return rc;
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
        ui_references(results, count, cfg->similarity_threshold);
    }

    printf(COLOR_GRAY "⏱ Search: %.2fs • Generation: %.2fs • Total: %.2fs\n" COLOR_RESET "\n",
           t_search - t_start, t_end - t_search, t_end - t_start);

    generation_result_free(&gen_res);
    db_free_results(results, count);
}

static void show_setup_walkthrough(void)
{
    printf("\n" COLOR_LAVENDER COLOR_BOLD "✨ Librarian Quick Start Walkthrough:" COLOR_RESET
           "\n\n");
    printf(COLOR_BOLD "1. Ingest your books and notes:" COLOR_RESET "\n");
    printf("   " COLOR_MINT "librarian ingest ~/Documents/my_library/" COLOR_RESET "\n");
    printf("   " COLOR_GRAY
           "Indexes PDF, EPUB, DOCX, ODT, HTML, Markdown, and plain text files.\n" COLOR_RESET);
    printf("   " COLOR_GRAY "Completely self-contained pure C99 engine with zero runtime "
           "dependencies.\n\n" COLOR_RESET);

    printf(COLOR_BOLD "2. Search and explore your library:" COLOR_RESET "\n");
    printf("   " COLOR_MINT "librarian docs" COLOR_RESET "\n");
    printf("   " COLOR_GRAY "Lists all indexed documents with IDs and chunk counts.\n" COLOR_RESET);
    printf("   " COLOR_MINT "librarian chunks 1" COLOR_RESET "\n");
    printf("   " COLOR_GRAY
           "Inspects the individual chunks and quotes that make up document #1.\n\n" COLOR_RESET);

    printf(COLOR_BOLD "3. Ask questions with verified citations:" COLOR_RESET "\n");
    printf("   " COLOR_MINT "librarian query \"What is quantum annealing?\"" COLOR_RESET "\n");
    printf("   " COLOR_GRAY
           "Synthesizes answers grounded strictly in your indexed library.\n\n" COLOR_RESET);

    printf(COLOR_BOLD "4. Interactive Chat Session:" COLOR_RESET "\n");
    printf("   " COLOR_MINT "librarian chat" COLOR_RESET "\n");
    printf("   " COLOR_GRAY "Interactive shell with auto-completion, live /docs, /chunks, /ingest, "
           "and /help.\n\n" COLOR_RESET);

    printf(COLOR_PEACH "💡 Tip: You can re-run this setup anytime with `librarian setup` or "
                       "`/setup` in chat.\n" COLOR_RESET "\n");
}

static int download_file(const char *url, const char *dest_path)
{
    char dir[512];
    strncpy(dir, dest_path, sizeof(dir) - 1);
    dir[sizeof(dir) - 1] = '\0';
    char *slash = strrchr(dir, '/');
#if defined(_WIN32)
    if (!slash)
        slash = strrchr(dir, '\\');
#endif
    if (slash) {
        *slash = '\0';
        char mkdir_cmd[2048];
#if defined(_WIN32)
        snprintf(mkdir_cmd, sizeof(mkdir_cmd), "if not exist \"%s\" mkdir \"%s\"", dir, dir);
#else
        snprintf(mkdir_cmd, sizeof(mkdir_cmd), "mkdir -p \"%s\"", dir);
#endif
        if (system(mkdir_cmd) != 0) {
            /* Handled: Directory may already exist or will fail on fopen */
        }
    }

    char curl_cmd[2048];
    snprintf(curl_cmd, sizeof(curl_cmd), "curl -L --progress-bar -C - \"%s\" -o \"%s\"", url,
             dest_path);
    printf(COLOR_BLUE "⬇ Downloading %s..." COLOR_RESET "\n", dest_path);
    int rc = system(curl_cmd);
    if (rc != 0) {
        ui_error("Download failed (curl exit code %d)", rc);
        return -1;
    }
    return 0;
}

static int subcmd_setup(librarian_config_t *cfg, bool force)
{
    printf(COLOR_LAVENDER COLOR_BOLD
           "\n╔═══════════════════════════════════════════════════════════════════════╗\n");
    printf("║                    Librarian Automated Setup                          ║\n");
    printf("╚═══════════════════════════════════════════════════════════════════════╝\n" COLOR_RESET
           "\n");

    if (system("curl --version >/dev/null 2>&1") != 0) {
        ui_error(
            "curl was not found in PATH. Please install curl to download models automatically.");
        return 1;
    }

    /* 1. Embedder */
    struct stat st;
    bool download_emb = true;
    if (stat(cfg->embed_model_path, &st) == 0 && st.st_size > 1000000) {
        if (!force) {
            ui_info("Embedding model already exists: %s (%.1f MB)", cfg->embed_model_path,
                    (double)st.st_size / (1024.0 * 1024.0));
            download_emb = ui_confirm("Do you want to re-download the embedding model? [y/N]:");
        }
    }
    if (download_emb) {
        const char *embed_url = "https://huggingface.co/SuperPauly/harrier-oss-v1-0.6b-gguf/"
                                "resolve/main/harrier-oss-v1-0.6b.Q8_0.gguf";
        if (download_file(embed_url, cfg->embed_model_path) != 0) {
            return 1;
        }
        ui_success("Embedding model ready at: %s", cfg->embed_model_path);
    }

    /* 2. Generator */
    bool download_gen = true;
    if (stat(cfg->gen_model_path, &st) == 0 && st.st_size > 1000000) {
        if (!force) {
            ui_info("Generation model already exists: %s (%.1f MB)", cfg->gen_model_path,
                    (double)st.st_size / (1024.0 * 1024.0));
            download_gen = ui_confirm("Do you want to re-download the generation model? [y/N]:");
        }
    }
    if (download_gen) {
        const char *gen_url = "https://huggingface.co/openbmb/MiniCPM-2B-dpo-bf16-gguf/resolve/"
                              "main/MiniCPM-2B-dpo-bf16.Q8_0.gguf";
        if (download_file(gen_url, cfg->gen_model_path) != 0) {
            return 1;
        }
        ui_success("Generation model ready at: %s", cfg->gen_model_path);
    }

    show_setup_walkthrough();
    return 0;
}

static int check_or_setup_models(librarian_config_t *cfg, bool need_generator)
{
    struct stat st;
    bool emb_missing = (stat(cfg->embed_model_path, &st) != 0 || st.st_size < 1000000);
    bool gen_missing =
        need_generator && (stat(cfg->gen_model_path, &st) != 0 || st.st_size < 1000000);

    if (emb_missing || gen_missing) {
        if (isatty(fileno(stdin))) {
            ui_warn("Required GGUF model files were not found on disk.");
            if (ui_confirm("Would you like to run automated setup to download default models from "
                           "Hugging Face? [Y/n]:")) {
                return subcmd_setup(cfg, false);
            }
        }
        if (emb_missing) {
            ui_error("Missing embedding model at '%s'. Run 'librarian setup' to download.",
                     cfg->embed_model_path);
        }
        if (gen_missing) {
            ui_error("Missing generator model at '%s'. Run 'librarian setup' to download.",
                     cfg->gen_model_path);
        }
        return -1;
    }
    return 0;
}

static int run_ingest_mode(librarian_config_t *cfg, const char *path)
{
    if (check_or_setup_models(cfg, false) != 0) {
        return 1;
    }

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

    ui_info("Ingesting path: %s (using %d threads)", path, embedder_get_thread_count(emb));
    double t_start = get_time_sec();
    int docs = 0, chunks = 0, skipped = 0, failed = 0;
    int rc = ingest_path(db, emb, path, cfg->embed_dimension, cfg->chunk_size_words,
                         cfg->chunk_overlap_words, &docs, &chunks, &skipped, &failed);
    double t_end = get_time_sec();

    if (rc != 0) {
        embedder_free(emb);
        db_close(db);
        return 1;
    }

    if (failed > 0) {
        ui_warn("Could not extract text from %d file(s) (empty, unsupported, or scanned image)",
                failed);
    }

    if (docs > 0) {
        if (skipped > 0) {
            ui_success(
                "Ingestion complete: %d new documents (%d chunks), %d unchanged files skipped "
                "(⏱ %.2fs)",
                docs, chunks, skipped, t_end - t_start);
        } else {
            ui_success("Ingestion complete: %d documents, %d total chunks stored (⏱ %.2fs)", docs,
                       chunks, t_end - t_start);
        }
    } else if (skipped > 0) {
        ui_info("No new documents ingested: %d unchanged files skipped (⏱ %.2fs)", skipped,
                t_end - t_start);
    } else if (failed > 0) {
        ui_warn("Ingested 0 documents (%d file(s) failed extraction) (⏱ %.2fs)", failed,
                t_end - t_start);
    } else {
        ui_warn("Ingested 0 documents (⏱ %.2fs)", t_end - t_start);
    }

    embedder_free(emb);
    db_close(db);
    return 0;
}

static int run_query_mode(librarian_config_t *cfg, const char *question)
{
    if (check_or_setup_models(cfg, true) != 0) {
        return 1;
    }

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
        ui_references(results, count, cfg->similarity_threshold);
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
    printf("  " COLOR_MINT "/docs [query]" COLOR_RESET
           "     List indexed documents matching optional pattern\n");
    printf("  " COLOR_MINT "/chunks <id> [n]" COLOR_RESET
           "  Inspect chunks belonging to a document\n");
    printf("  " COLOR_MINT "/ingest <path>" COLOR_RESET
           "   Ingest a document file or directory recursively\n");
    printf("  " COLOR_MINT "/stats" COLOR_RESET
           "           Display vector database document and chunk statistics\n");
    printf("  " COLOR_MINT "/config" COLOR_RESET
           "          Display current model thresholds and paths\n");
    printf("  " COLOR_MINT "/reload" COLOR_RESET
           "          Reload configuration from librarian.toml\n");
    printf("  " COLOR_MINT "/setup" COLOR_RESET
           "           Download models or display tutorial walkthrough\n");
    printf("  " COLOR_MINT "/debug" COLOR_RESET "           Toggle console debug diagnostics\n");
    printf("  " COLOR_MINT "/reset" COLOR_RESET
           "           Clear database (requires confirmation)\n");
    printf("  " COLOR_MINT "/clear" COLOR_RESET
           "           Clear screen and display welcome banner (or Ctrl+L)\n");
    printf("  " COLOR_MINT "/exit" COLOR_RESET ", " COLOR_MINT "/quit" COLOR_RESET
           "      Exit the interactive session\n");
    printf(COLOR_GRAY
           "  Enter any query to search documents and generate a verified answer.\n" COLOR_RESET
           "\n");
}

static int run_chat_mode(const librarian_config_t *initial_cfg)
{
    librarian_config_t cfg = *initial_cfg;
    if (check_or_setup_models(&cfg, true) != 0) {
        return 1;
    }

    ui_banner();

    ui_info("Opening vector database: %s", cfg.db_path);
    db_context_t *db = db_open(cfg.db_path);
    if (!db) {
        ui_error("Failed to open database");
        return 1;
    }
    db_init_schema(db, cfg.embed_dimension);

    ui_info("Loading embedder: %s", cfg.embed_model_path);
    embedder_context_t *emb = embedder_init(cfg.embed_model_path, cfg.embed_dimension);
    if (!emb) {
        db_close(db);
        return 1;
    }

    ui_info("Loading generator: %s", cfg.gen_model_path);
    generator_context_t *gen = generator_init(cfg.gen_model_path, cfg.gen_context_length);
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
            config_print(&cfg);
            continue;
        }

        if (strcmp(cmd, "/reload") == 0) {
            librarian_config_t new_cfg;
            if (config_load(CONFIG_DEFAULT_PATH, &new_cfg) == 0) {
                cfg = new_cfg;
                ui_success("Reloaded configuration from %s", CONFIG_DEFAULT_PATH);
                config_print(&cfg);
            } else {
                ui_error("Failed to reload configuration from %s", CONFIG_DEFAULT_PATH);
            }
            continue;
        }

        if (strcmp(cmd, "/setup") == 0) {
            subcmd_setup(&cfg, false);
            continue;
        }

        if (strcmp(cmd, "/debug") == 0) {
            bool echo = logger_get_console_echo();
            logger_set_console_echo(!echo);
            if (!echo) {
                ui_info("Debug mode enabled: engine diagnostics will stream to console");
            } else {
                ui_info("Debug mode disabled");
            }
            continue;
        }

        if (strcmp(cmd, "/reset") == 0) {
            if (ui_confirm(
                    "Are you sure you want to clear the entire knowledge database? [y/N]:")) {
                if (db_reset(db, cfg.embed_dimension) == 0) {
                    ui_success("Database cleared successfully. All documents and chunks deleted.");
                } else {
                    ui_error("Failed to reset database");
                }
            } else {
                ui_info("Database reset canceled.");
            }
            continue;
        }

        if (strcmp(cmd, "/stats") == 0) {
            int doc_count = 0, chunk_count = 0;
            db_get_stats(db, &doc_count, &chunk_count);
            printf("\n" COLOR_LAVENDER COLOR_BOLD "Database Statistics:" COLOR_RESET "\n");
            printf("  • Path: %s\n", cfg.db_path);
            printf("  • Documents: %d\n", doc_count);
            printf("  • Chunks: %d\n", chunk_count);
            printf("  • Vector Dimension: %d\n\n", cfg.embed_dimension);
            continue;
        }

        if (strcmp(cmd, "/docs") == 0 || strncmp(cmd, "/docs ", 6) == 0) {
            const char *pattern = NULL;
            if (strncmp(cmd, "/docs ", 6) == 0) {
                pattern = cmd + 6;
                while (*pattern && isspace((unsigned char)*pattern))
                    pattern++;
                if (*pattern == '\0')
                    pattern = NULL;
            }
            doc_info_t *docs = NULL;
            int count = 0;
            if (db_list_documents(db, pattern, &docs, &count) == 0) {
                ui_list_documents(docs, count, pattern);
                db_free_doc_info(docs, count);
            } else {
                ui_error("Failed to query indexed documents");
            }
            continue;
        }

        if (strcmp(cmd, "/chunks") == 0 || strncmp(cmd, "/chunks ", 8) == 0) {
            const char *args = cmd + 7;
            while (*args && isspace((unsigned char)*args))
                args++;
            if (*args == '\0') {
                ui_warn("Usage: /chunks <doc_id|filename_pattern> [limit]");
                continue;
            }

            int64_t target_doc_id = -1;
            char target_path[512] = {0};
            int limit = 20;

            char *endptr = NULL;
            long val = strtol(args, &endptr, 10);
            if (endptr != args && (*endptr == '\0' || isspace((unsigned char)*endptr))) {
                target_doc_id = val;
                while (*endptr && isspace((unsigned char)*endptr))
                    endptr++;
                if (*endptr) {
                    limit = atoi(endptr);
                }
            } else {
                char pat[256];
                int pidx = 0;
                while (*args && !isspace((unsigned char)*args) && pidx + 1 < (int)sizeof(pat)) {
                    pat[pidx++] = *args++;
                }
                pat[pidx] = '\0';
                while (*args && isspace((unsigned char)*args))
                    args++;
                if (*args) {
                    limit = atoi(args);
                }

                doc_info_t *docs = NULL;
                int count = 0;
                if (db_list_documents(db, pat, &docs, &count) == 0 && count > 0) {
                    target_doc_id = docs[0].id;
                    snprintf(target_path, sizeof(target_path), "%s", docs[0].path);
                    db_free_doc_info(docs, count);
                } else {
                    if (docs) {
                        db_free_doc_info(docs, count);
                    }
                    ui_warn("No documents matched pattern '%s'", pat);
                    continue;
                }
            }

            if (target_path[0] == '\0') {
                doc_info_t *docs = NULL;
                int count = 0;
                if (db_list_documents(db, NULL, &docs, &count) == 0) {
                    for (int i = 0; i < count; i++) {
                        if (docs[i].id == target_doc_id) {
                            snprintf(target_path, sizeof(target_path), "%s", docs[i].path);
                            break;
                        }
                    }
                    db_free_doc_info(docs, count);
                }
            }

            chunk_info_t *chunks = NULL;
            int count = 0;
            if (db_get_document_chunks(db, target_doc_id, &chunks, &count) == 0) {
                ui_list_chunks(target_doc_id, target_path, chunks, count, limit);
                db_free_chunk_info(chunks, count);
            } else {
                ui_error("Failed to retrieve chunks for document #%lld", (long long)target_doc_id);
            }
            continue;
        }

        if (strcmp(cmd, "/ingest") == 0 || strncmp(cmd, "/ingest ", 8) == 0) {
            const char *target = (strncmp(cmd, "/ingest ", 8) == 0) ? cmd + 8 : "";
            char target_clean[2048];
            doc_clean_path(target, target_clean, sizeof(target_clean));

            if (target_clean[0]) {
                ui_info("Ingesting: %s", target_clean);
                double t0 = get_time_sec();
                int docs = 0, chunks = 0, skipped = 0, failed = 0;
                int rc =
                    ingest_path(db, emb, target_clean, cfg.embed_dimension, cfg.chunk_size_words,
                                cfg.chunk_overlap_words, &docs, &chunks, &skipped, &failed);
                double t1 = get_time_sec();

                if (rc != 0) {
                    continue;
                }

                if (failed > 0) {
                    ui_warn("Could not extract text from %d file(s) (empty, unsupported, or "
                            "scanned image)",
                            failed);
                }

                if (docs > 0) {
                    if (skipped > 0) {
                        ui_success(
                            "Ingested %d new documents (%d chunks), %d unchanged files skipped "
                            "(⏱ %.2fs)",
                            docs, chunks, skipped, t1 - t0);
                    } else {
                        ui_success("Ingested %d documents, %d total chunks (⏱ %.2fs)", docs, chunks,
                                   t1 - t0);
                    }
                } else if (skipped > 0) {
                    ui_info("No new documents ingested: %d unchanged files skipped (⏱ %.2fs)",
                            skipped, t1 - t0);
                } else if (failed > 0) {
                    ui_warn("Ingested 0 documents (%d file(s) failed extraction) (⏱ %.2fs)", failed,
                            t1 - t0);
                } else {
                    ui_warn("Ingested 0 documents (⏱ %.2fs)", t1 - t0);
                }
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
        run_query_core(db, emb, gen, &cfg, cmd);
    }

    repl_free(repl);
    generator_free(gen);
    embedder_free(emb);
    db_close(db);
    return 0;
}

static int run_reset_mode(const librarian_config_t *cfg, bool force)
{
    if (!force) {
        printf("\n" COLOR_PEACH COLOR_BOLD "Database: " COLOR_RESET "%s\n", cfg->db_path);
        if (!ui_confirm("Are you sure you want to clear the entire knowledge database? [y/N]:")) {
            ui_info("Database reset canceled.");
            return 0;
        }
    }

    db_context_t *db = db_open(cfg->db_path);
    if (!db) {
        ui_error("Failed to open database %s", cfg->db_path);
        return 1;
    }

    if (db_reset(db, cfg->embed_dimension) != 0) {
        ui_error("Failed to reset database %s", cfg->db_path);
        db_close(db);
        return 1;
    }

    db_close(db);
    ui_success("Database cleared successfully: %s", cfg->db_path);
    return 0;
}

static int run_docs_mode(const librarian_config_t *cfg, const char *search_pattern)
{
    db_context_t *db = db_open(cfg->db_path);
    if (!db) {
        ui_error("Failed to open database %s", cfg->db_path);
        return 1;
    }

    doc_info_t *docs = NULL;
    int count = 0;
    int rc = db_list_documents(db, search_pattern, &docs, &count);
    if (rc == 0) {
        ui_list_documents(docs, count, search_pattern);
        db_free_doc_info(docs, count);
    } else {
        ui_error("Failed to query documents from database");
    }

    db_close(db);
    return rc;
}

static int run_chunks_mode(const librarian_config_t *cfg, const char *target, const char *limit_str)
{
    if (!target) {
        ui_error(
            "Missing document ID or pattern. Usage: librarian chunks <doc_id|pattern> [limit]");
        return 1;
    }

    db_context_t *db = db_open(cfg->db_path);
    if (!db) {
        ui_error("Failed to open database %s", cfg->db_path);
        return 1;
    }

    int limit = 20;
    if (limit_str && limit_str[0] != '\0') {
        limit = atoi(limit_str);
    }

    int64_t target_doc_id = -1;
    char target_path[512] = {0};

    char *endptr = NULL;
    long val = strtol(target, &endptr, 10);
    if (endptr != target && *endptr == '\0') {
        target_doc_id = val;
    } else {
        doc_info_t *docs = NULL;
        int count = 0;
        if (db_list_documents(db, target, &docs, &count) == 0 && count > 0) {
            target_doc_id = docs[0].id;
            snprintf(target_path, sizeof(target_path), "%s", docs[0].path);
            db_free_doc_info(docs, count);
        } else {
            if (docs) {
                db_free_doc_info(docs, count);
            }
            ui_warn("No documents found matching pattern '%s'", target);
            db_close(db);
            return 1;
        }
    }

    if (target_path[0] == '\0') {
        doc_info_t *docs = NULL;
        int count = 0;
        if (db_list_documents(db, NULL, &docs, &count) == 0) {
            for (int i = 0; i < count; i++) {
                if (docs[i].id == target_doc_id) {
                    snprintf(target_path, sizeof(target_path), "%s", docs[i].path);
                    break;
                }
            }
            db_free_doc_info(docs, count);
        }
    }

    chunk_info_t *chunks = NULL;
    int count = 0;
    int rc = db_get_document_chunks(db, target_doc_id, &chunks, &count);
    if (rc == 0) {
        ui_list_chunks(target_doc_id, target_path, chunks, count, limit);
        db_free_chunk_info(chunks, count);
    } else {
        ui_error("Failed to query chunks for document #%lld", (long long)target_doc_id);
    }

    db_close(db);
    return rc;
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
    printf("  %s docs [pattern]         List indexed documents matching optional pattern\n", prog);
    printf("  %s chunks <id> [limit]    Inspect chunks belonging to an indexed document\n", prog);
    printf("  %s setup [-f|--force]     Download default models and view quickstart guide\n", prog);
    printf("  %s reset [-f|--force]     Clear all indexed documents and chunks\n", prog);
    printf("  %s --version              Display version information\n", prog);
    printf("  %s --help                 Display this help menu\n\n", prog);
    printf(COLOR_BOLD "OPTIONS:" COLOR_RESET "\n");
    printf("  -d, --debug               Enable verbose engine debug logs to console\n");
    printf("  -f, --force               Bypass confirmation prompt on reset\n");
    printf("  -h, --help                Show help instructions\n");
    printf("  -v, --version             Show version and build details\n\n");
}

int main(int argc, char **argv)
{
    if (argc < 2) {
        print_usage(argv[0]);
        return 0;
    }

    bool debug_mode = false;
    bool force_flag = false;
    const char *command = NULL;
    const char *arg_value = NULL;
    const char *arg_extra = NULL;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--debug") == 0 || strcmp(argv[i], "-d") == 0) {
            debug_mode = true;
        } else if (strcmp(argv[i], "--force") == 0 || strcmp(argv[i], "-f") == 0) {
            force_flag = true;
        } else if (strcmp(argv[i], "--version") == 0 || strcmp(argv[i], "-v") == 0) {
            printf("librarian version %s (C99, sqlite-vec, llama.cpp)\n", LIBRARIAN_VERSION);
            return 0;
        } else if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            print_usage(argv[0]);
            return 0;
        } else if (argv[i][0] != '-') {
            if (!command) {
                command = argv[i];
            } else if (!arg_value) {
                arg_value = argv[i];
            } else if (!arg_extra) {
                arg_extra = argv[i];
            }
        }
    }

    if (!command) {
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

    if (debug_mode) {
        logger_set_console_echo(true);
        logger_debug("Debug mode enabled via command line");
    }

    int rc = 0;
    if (strcmp(command, "ingest") == 0) {
        if (!arg_value) {
            ui_error("Missing path to ingest. Usage: %s ingest <file_or_dir>", argv[0]);
            logger_close();
            return 1;
        }
        rc = run_ingest_mode(&cfg, arg_value);
    } else if (strcmp(command, "query") == 0) {
        if (!arg_value) {
            ui_error("Missing query string. Usage: %s query \"<question>\"", argv[0]);
            logger_close();
            return 1;
        }
        rc = run_query_mode(&cfg, arg_value);
    } else if (strcmp(command, "chat") == 0) {
        rc = run_chat_mode(&cfg);
    } else if (strcmp(command, "docs") == 0) {
        rc = run_docs_mode(&cfg, arg_value);
    } else if (strcmp(command, "chunks") == 0) {
        rc = run_chunks_mode(&cfg, arg_value, arg_extra);
    } else if (strcmp(command, "setup") == 0) {
        rc = subcmd_setup(&cfg, force_flag);
    } else if (strcmp(command, "reset") == 0) {
        rc = run_reset_mode(&cfg, force_flag);
    } else {
        ui_error("Unknown command '%s'. Run '%s --help' for usage.", command, argv[0]);
        rc = 1;
    }

    logger_close();
    return rc;
}
