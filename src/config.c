#include "config.h"

#include "toml.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#ifdef _WIN32
#include <direct.h>
#define mkdir_portable(p) _mkdir(p)
#else
#define mkdir_portable(p) mkdir(p, 0755)
#endif

int config_get_default_paths(char *config_path, size_t cfg_sz, char *data_dir, size_t data_sz)
{
    if (!config_path || cfg_sz == 0 || !data_dir || data_sz == 0) {
        return -1;
    }

#ifdef _WIN32
    const char *appdata = getenv("APPDATA");
    const char *localappdata = getenv("LOCALAPPDATA");
    if (!appdata) {
        appdata = "C:\\ProgramData";
    }
    if (!localappdata) {
        localappdata = appdata;
    }
    snprintf(config_path, cfg_sz, "%s\\librarian\\librarian.toml", appdata);
    snprintf(data_dir, data_sz, "%s\\librarian", localappdata);
#elif defined(__APPLE__)
    const char *home = getenv("HOME");
    if (!home) {
        home = "/tmp";
    }
    snprintf(config_path, cfg_sz, "%s/Library/Application Support/librarian/librarian.toml", home);
    snprintf(data_dir, data_sz, "%s/Library/Application Support/librarian", home);
#else
    const char *xdg_cfg = getenv("XDG_CONFIG_HOME");
    const char *xdg_data = getenv("XDG_DATA_HOME");
    const char *home = getenv("HOME");
    if (xdg_cfg && xdg_cfg[0]) {
        snprintf(config_path, cfg_sz, "%s/librarian/librarian.toml", xdg_cfg);
    } else if (home && home[0]) {
        snprintf(config_path, cfg_sz, "%s/.config/librarian/librarian.toml", home);
    } else {
        snprintf(config_path, cfg_sz, "librarian.toml");
    }

    if (xdg_data && xdg_data[0]) {
        snprintf(data_dir, data_sz, "%s/librarian", xdg_data);
    } else if (home && home[0]) {
        snprintf(data_dir, data_sz, "%s/.local/share/librarian", home);
    } else {
        snprintf(data_dir, data_sz, "data");
    }
#endif
    return 0;
}

int config_save(const char *path, const librarian_config_t *cfg)
{
    if (!path || !cfg) {
        return -1;
    }

    /* Ensure parent directory exists */
    char dir[512];
    strncpy(dir, path, sizeof(dir) - 1);
    dir[sizeof(dir) - 1] = '\0';
    char *slash = strrchr(dir, '/');
#ifdef _WIN32
    if (!slash)
        slash = strrchr(dir, '\\');
#endif
    if (slash) {
        *slash = '\0';
        mkdir_portable(dir);
    }

    FILE *fp = fopen(path, "w");
    if (!fp) {
        return -1;
    }

    fprintf(fp, "[database]\npath = \"%s\"\n\n", cfg->db_path);
    fprintf(fp,
            "[embedder]\nmodel_path = \"%s\"\ndimension = %d\nsimilarity_threshold = %.2f\n"
            "chunk_size_words = %d\nchunk_overlap_words = %d\n\n",
            cfg->embed_model_path, cfg->embed_dimension, (double)cfg->similarity_threshold,
            cfg->chunk_size_words, cfg->chunk_overlap_words);
    fprintf(fp,
            "[generator]\nmodel_path = \"%s\"\ncontext_length = %d\nconfidence_threshold = %.2f\n",
            cfg->gen_model_path, cfg->gen_context_length, (double)cfg->confidence_threshold);

    fclose(fp);
    return 0;
}

void config_default(librarian_config_t *cfg)
{
    if (!cfg) {
        return;
    }
    memset(cfg, 0, sizeof(*cfg));

    char sys_cfg[512] = {0};
    char sys_data[512] = {0};
    config_get_default_paths(sys_cfg, sizeof(sys_cfg), sys_data, sizeof(sys_data));

    /* If running locally with data/ folder, prefer relative paths */
    struct stat st;
    if (stat("librarian.toml", &st) == 0 || stat("data", &st) == 0) {
        snprintf(cfg->db_path, sizeof(cfg->db_path), "data/librarian.db");
        snprintf(cfg->log_path, sizeof(cfg->log_path), "data/librarian.log");
        snprintf(cfg->embed_model_path, sizeof(cfg->embed_model_path),
                 "models/harrier-oss-v1-0.6b.Q8_0.gguf");
        snprintf(cfg->gen_model_path, sizeof(cfg->gen_model_path), "models/MiniCPM5-2B-Q8_0.gguf");
    } else {
        snprintf(cfg->db_path, sizeof(cfg->db_path), "%s/librarian.db", sys_data);
        snprintf(cfg->log_path, sizeof(cfg->log_path), "%s/librarian.log", sys_data);
        snprintf(cfg->embed_model_path, sizeof(cfg->embed_model_path),
                 "%s/models/harrier-oss-v1-0.6b.Q8_0.gguf", sys_data);
        snprintf(cfg->gen_model_path, sizeof(cfg->gen_model_path),
                 "%s/models/MiniCPM5-2B-Q8_0.gguf", sys_data);
    }

    cfg->embed_dimension = 1024;
    cfg->similarity_threshold = 0.65f;
    cfg->chunk_size_words = 250;
    cfg->chunk_overlap_words = 40;
    cfg->gen_context_length = 4096;
    cfg->confidence_threshold = 0.50f;
}

/* Copy a TOML string datum into DST (SIZE bytes, always terminated) and free
 * it. A missing key leaves DST at its default. */
static void take_string(char *dst, size_t size, toml_datum_t d)
{
    if (!d.ok || !d.u.s) {
        return;
    }
    strncpy(dst, d.u.s, size - 1);
    dst[size - 1] = '\0';
    free(d.u.s);
}

int config_load(const char *path, librarian_config_t *cfg)
{
    if (!cfg) {
        return -1;
    }
    config_default(cfg);

    const char *actual_path = path;
    char sys_cfg[512] = {0};
    char sys_data[512] = {0};

    if (!actual_path || actual_path[0] == '\0') {
        struct stat st;
        if (stat("librarian.toml", &st) == 0) {
            actual_path = "librarian.toml";
        } else {
            config_get_default_paths(sys_cfg, sizeof(sys_cfg), sys_data, sizeof(sys_data));
            if (stat(sys_cfg, &st) == 0) {
                actual_path = sys_cfg;
            } else {
                actual_path = "librarian.toml";
            }
        }
    }

    FILE *fp = fopen(actual_path, "r");
    if (!fp) {
        return -1;
    }

    char errbuf[256] = {0};
    toml_table_t *root = toml_parse_file(fp, errbuf, sizeof(errbuf));
    fclose(fp);

    if (!root) {
        fprintf(stderr, "Error parsing TOML config %s: %s\n", actual_path, errbuf);
        return -2;
    }

    /* [database] */
    const toml_table_t *tab_db = toml_table_in(root, "database");
    if (tab_db) {
        toml_datum_t d_path = toml_string_in(tab_db, "path");
        take_string(cfg->db_path, sizeof(cfg->db_path), d_path);
    }

    /* [embedder] */
    const toml_table_t *tab_emb = toml_table_in(root, "embedder");
    if (tab_emb) {
        toml_datum_t d_path = toml_string_in(tab_emb, "model_path");
        take_string(cfg->embed_model_path, sizeof(cfg->embed_model_path), d_path);
        toml_datum_t d_dim = toml_int_in(tab_emb, "dimension");
        if (d_dim.ok) {
            cfg->embed_dimension = (int)d_dim.u.i;
        }
        toml_datum_t d_sim = toml_double_in(tab_emb, "similarity_threshold");
        if (d_sim.ok) {
            cfg->similarity_threshold = (float)d_sim.u.d;
        }
        toml_datum_t d_cs = toml_int_in(tab_emb, "chunk_size_words");
        if (d_cs.ok && d_cs.u.i > 0) {
            cfg->chunk_size_words = (int)d_cs.u.i;
        }
        toml_datum_t d_co = toml_int_in(tab_emb, "chunk_overlap_words");
        if (d_co.ok && d_co.u.i >= 0) {
            cfg->chunk_overlap_words = (int)d_co.u.i;
        }
    }

    /* [generator] */
    const toml_table_t *tab_gen = toml_table_in(root, "generator");
    if (tab_gen) {
        toml_datum_t d_path = toml_string_in(tab_gen, "model_path");
        take_string(cfg->gen_model_path, sizeof(cfg->gen_model_path), d_path);
        toml_datum_t d_ctx = toml_int_in(tab_gen, "context_length");
        if (d_ctx.ok) {
            cfg->gen_context_length = (int)d_ctx.u.i;
        }
        toml_datum_t d_conf = toml_double_in(tab_gen, "confidence_threshold");
        if (d_conf.ok) {
            cfg->confidence_threshold = (float)d_conf.u.d;
        }
    }

    /* [logging] or [system] */
    const toml_table_t *tab_log = toml_table_in(root, "logging");
    if (!tab_log) {
        tab_log = toml_table_in(root, "system");
    }
    if (tab_log) {
        toml_datum_t d_log = toml_string_in(tab_log, "log_path");
        if (!d_log.ok) {
            d_log = toml_string_in(tab_log, "path");
        }
        take_string(cfg->log_path, sizeof(cfg->log_path), d_log);
    }

    toml_free(root);
    return 0;
}

void config_print(const librarian_config_t *cfg)
{
    if (!cfg) {
        return;
    }
    printf("Configuration:\n");
    printf("  [database] path = %s\n", cfg->db_path);
    printf("  [logging] path = %s\n", cfg->log_path);
    printf("  [embedder] model = %s (dim=%d, sim_thresh=%.2f, chunk_size=%d, overlap=%d)\n",
           cfg->embed_model_path, cfg->embed_dimension, (double)cfg->similarity_threshold,
           cfg->chunk_size_words, cfg->chunk_overlap_words);
    printf("  [generator] model = %s (ctx=%d, conf_thresh=%.2f)\n", cfg->gen_model_path,
           cfg->gen_context_length, (double)cfg->confidence_threshold);
}
