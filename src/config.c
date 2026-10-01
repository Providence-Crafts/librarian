#include "config.h"

#include "toml.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void config_default(librarian_config_t *cfg)
{
    if (!cfg)
        return;
    memset(cfg, 0, sizeof(*cfg));

    snprintf(cfg->db_path, sizeof(cfg->db_path), "data/librarian.db");

    snprintf(cfg->embed_model_path, sizeof(cfg->embed_model_path),
             "models/harrier-oss-v1-0.6b.Q8_0.gguf");
    cfg->embed_dimension = 1024;
    cfg->similarity_threshold = 0.65f;

    snprintf(cfg->gen_model_path, sizeof(cfg->gen_model_path), "models/MiniCPM5-2B-Q8_0.gguf");
    cfg->gen_context_length = 2048;
    cfg->confidence_threshold = 0.50f;
}

int config_load(const char *path, librarian_config_t *cfg)
{
    config_default(cfg);

    FILE *fp = fopen(path, "r");
    if (!fp) {
        return -1;
    }

    char errbuf[256] = {0};
    toml_table_t *root = toml_parse_file(fp, errbuf, sizeof(errbuf));
    fclose(fp);

    if (!root) {
        fprintf(stderr, "Error parsing TOML config %s: %s\n", path, errbuf);
        return -2;
    }

    /* [database] */
    const toml_table_t *tab_db = toml_table_in(root, "database");
    if (tab_db) {
        toml_datum_t d_path = toml_string_in(tab_db, "path");
        if (d_path.ok) {
            strncpy(cfg->db_path, d_path.u.s, sizeof(cfg->db_path) - 1);
            cfg->db_path[sizeof(cfg->db_path) - 1] = '\0';
            free(d_path.u.s);
        }
    }

    /* [embedder] */
    const toml_table_t *tab_emb = toml_table_in(root, "embedder");
    if (tab_emb) {
        toml_datum_t d_path = toml_string_in(tab_emb, "model_path");
        if (d_path.ok) {
            strncpy(cfg->embed_model_path, d_path.u.s, sizeof(cfg->embed_model_path) - 1);
            cfg->embed_model_path[sizeof(cfg->embed_model_path) - 1] = '\0';
            free(d_path.u.s);
        }
        toml_datum_t d_dim = toml_int_in(tab_emb, "dimension");
        if (d_dim.ok) {
            cfg->embed_dimension = (int)d_dim.u.i;
        }
        toml_datum_t d_sim = toml_double_in(tab_emb, "similarity_threshold");
        if (d_sim.ok) {
            cfg->similarity_threshold = (float)d_sim.u.d;
        }
    }

    /* [generator] */
    const toml_table_t *tab_gen = toml_table_in(root, "generator");
    if (tab_gen) {
        toml_datum_t d_path = toml_string_in(tab_gen, "model_path");
        if (d_path.ok) {
            strncpy(cfg->gen_model_path, d_path.u.s, sizeof(cfg->gen_model_path) - 1);
            cfg->gen_model_path[sizeof(cfg->gen_model_path) - 1] = '\0';
            free(d_path.u.s);
        }
        toml_datum_t d_ctx = toml_int_in(tab_gen, "context_length");
        if (d_ctx.ok) {
            cfg->gen_context_length = (int)d_ctx.u.i;
        }
        toml_datum_t d_conf = toml_double_in(tab_gen, "confidence_threshold");
        if (d_conf.ok) {
            cfg->confidence_threshold = (float)d_conf.u.d;
        }
    }

    toml_free(root);
    return 0;
}

void config_print(const librarian_config_t *cfg)
{
    if (!cfg)
        return;
    printf("Configuration:\n");
    printf("  [database] path = %s\n", cfg->db_path);
    printf("  [embedder] model = %s (dim=%d, sim_thresh=%.2f)\n", cfg->embed_model_path,
           cfg->embed_dimension, (double)cfg->similarity_threshold);
    printf("  [generator] model = %s (ctx=%d, conf_thresh=%.2f)\n", cfg->gen_model_path,
           cfg->gen_context_length, (double)cfg->confidence_threshold);
}
