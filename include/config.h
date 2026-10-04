#ifndef LIBRARIAN_CONFIG_H
#define LIBRARIAN_CONFIG_H

#include <stdbool.h>
#include <stddef.h>

typedef struct {
    char db_path[512];
    char log_path[512];

    /* Embedder settings */
    char embed_model_path[512];
    int embed_dimension;
    float similarity_threshold;
    int chunk_size_words;
    int chunk_overlap_words;

    /* Generator settings */
    char gen_model_path[512];
    int gen_context_length;
    float confidence_threshold;
} librarian_config_t;

void config_default(librarian_config_t *cfg);
int config_get_default_paths(char *config_path, size_t cfg_sz, char *data_dir, size_t data_sz);
/* Fills cfg with defaults, then overrides them from path. A NULL path searches
 * ./librarian.toml, then the per-user config file; finding neither is not an
 * error. Returns -1 if the file cannot be opened, -2 on a parse error. */
int config_load(const char *path, librarian_config_t *cfg);
int config_save(const char *path, const librarian_config_t *cfg);
void config_print(const librarian_config_t *cfg);

#endif /* LIBRARIAN_CONFIG_H */
