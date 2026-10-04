#include "config.h"
#include "minunit.h"

#include <math.h>
#include <string.h>

const char *test_config_defaults(void);
const char *test_config_load_file(void);
const char *test_config_save_and_paths(void);

const char *test_config_defaults(void)
{
    librarian_config_t cfg;
    config_default(&cfg);

    mu_assert("default db path should be data/librarian.db",
              strcmp(cfg.db_path, "data/librarian.db") == 0);
    mu_assert("default embed dimension should be 1024", cfg.embed_dimension == 1024);
    mu_assert("default sim threshold should be ~0.65",
              fabsf(cfg.similarity_threshold - 0.65f) < 0.001f);
    mu_assert("default context length should be 4096", cfg.gen_context_length == 4096);
    mu_assert("default conf threshold should be ~0.50",
              fabsf(cfg.confidence_threshold - 0.50f) < 0.001f);

    return NULL;
}

const char *test_config_load_file(void)
{
    librarian_config_t cfg;
    int rc = config_load("librarian.toml", &cfg);
    mu_assert("config_load should succeed on librarian.toml", rc == 0);

    mu_assert("db_path match", strcmp(cfg.db_path, "data/librarian.db") == 0);
    mu_assert("embed_model_path match",
              strcmp(cfg.embed_model_path, "models/harrier-oss-v1-0.6b.Q8_0.gguf") == 0);
    mu_assert("embed dimension match", cfg.embed_dimension == 1024);
    mu_assert("gen_model_path match",
              strcmp(cfg.gen_model_path, "models/MiniCPM5-2B-Q8_0.gguf") == 0);
    mu_assert("gen context length match", cfg.gen_context_length == 4096);

    return NULL;
}

const char *test_config_save_and_paths(void)
{
    char cfg_path[512] = {0};
    char data_dir[512] = {0};
    int rc = config_get_default_paths(cfg_path, sizeof(cfg_path), data_dir, sizeof(data_dir));
    mu_assert("config_get_default_paths succeeded", rc == 0);
    mu_assert("config_path not empty", strlen(cfg_path) > 0);
    mu_assert("data_dir not empty", strlen(data_dir) > 0);

    librarian_config_t cfg;
    config_default(&cfg);

    const char *tmp_toml = "/tmp/test_librarian_saved.toml";
    rc = config_save(tmp_toml, &cfg);
    mu_assert("config_save succeeded", rc == 0);

    librarian_config_t loaded;
    rc = config_load(tmp_toml, &loaded);
    mu_assert("config_load of saved file succeeded", rc == 0);
    mu_assert("db_path matches", strcmp(loaded.db_path, cfg.db_path) == 0);
    mu_assert("embed dimension matches", loaded.embed_dimension == cfg.embed_dimension);
    mu_assert("context length matches", loaded.gen_context_length == cfg.gen_context_length);

    remove(tmp_toml);
    return NULL;
}
