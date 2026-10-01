#ifndef LIBRARIAN_CONFIG_H
#define LIBRARIAN_CONFIG_H

#include <stdbool.h>

#define CONFIG_DEFAULT_PATH "librarian.toml"

typedef struct {
    char db_path[512];

    /* Embedder settings */
    char embed_model_path[512];
    int embed_dimension;
    float similarity_threshold;

    /* Generator settings */
    char gen_model_path[512];
    int gen_context_length;
    float confidence_threshold;
} librarian_config_t;

void config_default(librarian_config_t *cfg);
int config_load(const char *path, librarian_config_t *cfg);
void config_print(const librarian_config_t *cfg);

#endif /* LIBRARIAN_CONFIG_H */
