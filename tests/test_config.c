#include "config.h"
#include "minunit.h"

#include <math.h>
#include <string.h>

const char *test_config_defaults(void);
const char *test_config_load_file(void);

const char *test_config_defaults(void)
{
    librarian_config_t cfg;
    config_default(&cfg);

    mu_assert("default db path should be data/librarian.db",
              strcmp(cfg.db_path, "data/librarian.db") == 0);
    mu_assert("default embed dimension should be 1024", cfg.embed_dimension == 1024);
    mu_assert("default sim threshold should be ~0.65",
              fabsf(cfg.similarity_threshold - 0.65f) < 0.001f);
    mu_assert("default context length should be 2048", cfg.gen_context_length == 2048);
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
    mu_assert("gen context length match", cfg.gen_context_length == 2048);

    return NULL;
}
