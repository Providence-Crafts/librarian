#include "embedder.h"

#include "llama.h"

#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct embedder_context {
    struct llama_model *model;
    struct llama_context *ctx;
    int dimension;
};

void vector_normalize_l2(float *vec, int dim)
{
    if (!vec || dim <= 0)
        return;
    double sum = 0.0;
    for (int i = 0; i < dim; i++) {
        sum += (double)vec[i] * (double)vec[i];
    }
    double norm = sqrt(sum);
    if (norm > 1e-12) {
        for (int i = 0; i < dim; i++) {
            vec[i] = (float)((double)vec[i] / norm);
        }
    }
}

chunk_list_t chunk_text(const char *text, int chunk_size_words, int overlap_words)
{
    chunk_list_t list;
    list.chunks = NULL;
    list.count = 0;

    if (!text || chunk_size_words <= 0)
        return list;
    if (overlap_words < 0)
        overlap_words = 0;
    if (overlap_words >= chunk_size_words)
        overlap_words = chunk_size_words - 1;

    /* First pass: count words and record offsets */
    size_t len = strlen(text);
    if (len == 0)
        return list;

    int max_words = 16384;
    const char **word_starts = malloc(sizeof(char *) * (size_t)max_words);
    size_t *word_lens = malloc(sizeof(size_t) * (size_t)max_words);
    if (!word_starts || !word_lens) {
        free(word_starts);
        free(word_lens);
        return list;
    }

    int word_count = 0;
    size_t idx = 0;
    while (idx < len) {
        while (idx < len && isspace((unsigned char)text[idx]))
            idx++;
        if (idx >= len)
            break;

        size_t start = idx;
        while (idx < len && !isspace((unsigned char)text[idx]))
            idx++;
        size_t wlen = idx - start;

        if (word_count >= max_words) {
            max_words *= 2;
            const char **wstarts_new = realloc(word_starts, sizeof(char *) * (size_t)max_words);
            size_t *wlens_new = realloc(word_lens, sizeof(size_t) * (size_t)max_words);
            if (!wstarts_new || !wlens_new)
                break;
            word_starts = wstarts_new;
            word_lens = wlens_new;
        }

        word_starts[word_count] = text + start;
        word_lens[word_count] = wlen;
        word_count++;
    }

    if (word_count == 0) {
        free(word_starts);
        free(word_lens);
        return list;
    }

    /* Compute chunks */
    int step = chunk_size_words - overlap_words;
    if (step <= 0)
        step = 1;

    int chunk_cap = (word_count / step) + 2;
    list.chunks = malloc(sizeof(char *) * (size_t)chunk_cap);
    if (!list.chunks) {
        free(word_starts);
        free(word_lens);
        return list;
    }

    for (int w = 0; w < word_count; w += step) {
        int count_in_chunk = chunk_size_words;
        if (w + count_in_chunk > word_count) {
            count_in_chunk = word_count - w;
        }

        /* Calculate required string buffer size */
        size_t buf_sz = 0;
        for (int i = 0; i < count_in_chunk; i++) {
            buf_sz += word_lens[w + i] + 1; /* word + space or null */
        }

        char *chunk_str = malloc(buf_sz + 1);
        if (!chunk_str)
            break;

        size_t pos = 0;
        for (int i = 0; i < count_in_chunk; i++) {
            if (i > 0)
                chunk_str[pos++] = ' ';
            memcpy(chunk_str + pos, word_starts[w + i], word_lens[w + i]);
            pos += word_lens[w + i];
        }
        chunk_str[pos] = '\0';

        list.chunks[list.count++] = chunk_str;
        if (w + count_in_chunk >= word_count)
            break;
    }

    free(word_starts);
    free(word_lens);
    return list;
}

void chunk_list_free(chunk_list_t *list)
{
    if (!list || !list->chunks)
        return;
    for (int i = 0; i < list->count; i++) {
        free(list->chunks[i]);
    }
    free(list->chunks);
    list->chunks = NULL;
    list->count = 0;
}

embedder_context_t *embedder_init(const char *model_path, int dimension)
{
    if (!model_path)
        return NULL;

    llama_backend_init();

    struct llama_model_params mparams = llama_model_default_params();
    struct llama_model *model = llama_model_load_from_file(model_path, mparams);
    if (!model) {
        fprintf(stderr, "Failed to load embedding model from %s\n", model_path);
        return NULL;
    }

    struct llama_context_params cparams = llama_context_default_params();
    cparams.embeddings = true;
    cparams.pooling_type = LLAMA_POOLING_TYPE_MEAN;
    cparams.n_ctx = 2048;
    cparams.n_batch = 2048;

    struct llama_context *ctx = llama_init_from_model(model, cparams);
    if (!ctx) {
        fprintf(stderr, "Failed to initialize embedding context\n");
        llama_model_free(model);
        return NULL;
    }

    embedder_context_t *emb = calloc(1, sizeof(*emb));
    if (!emb) {
        llama_free(ctx);
        llama_model_free(model);
        return NULL;
    }

    emb->model = model;
    emb->ctx = ctx;
    int model_dim = llama_model_n_embd_out(model);
    emb->dimension = (model_dim > 0) ? model_dim : dimension;

    return emb;
}

void embedder_free(embedder_context_t *ctx)
{
    if (!ctx)
        return;
    if (ctx->ctx) {
        llama_free(ctx->ctx);
    }
    if (ctx->model) {
        llama_model_free(ctx->model);
    }
    free(ctx);
}

int embedder_embed(embedder_context_t *ctx, const char *text, float *out_vec)
{
    if (!ctx || !ctx->ctx || !ctx->model || !text || !out_vec)
        return -1;

    const struct llama_vocab *vocab = llama_model_get_vocab(ctx->model);
    int text_len = (int)strlen(text);
    int n_tokens_alloc = text_len + 16;
    if (n_tokens_alloc < 64)
        n_tokens_alloc = 64;

    llama_token *tokens = malloc(sizeof(llama_token) * (size_t)n_tokens_alloc);
    if (!tokens)
        return -1;

    int n_tokens = llama_tokenize(vocab, text, text_len, tokens, n_tokens_alloc, true, true);
    if (n_tokens < 0) {
        n_tokens_alloc = -n_tokens;
        llama_token *grown = realloc(tokens, sizeof(llama_token) * (size_t)n_tokens_alloc);
        if (!grown) {
            free(tokens);
            return -1;
        }
        tokens = grown;
        n_tokens = llama_tokenize(vocab, text, text_len, tokens, n_tokens_alloc, true, true);
        if (n_tokens < 0) {
            free(tokens);
            return -1;
        }
    }

    if (n_tokens == 0) {
        free(tokens);
        memset(out_vec, 0, sizeof(float) * (size_t)ctx->dimension);
        return 0;
    }

    /* Clear memory states before new sequence */
    llama_memory_clear(llama_get_memory(ctx->ctx), true);

    struct llama_batch batch = llama_batch_init(n_tokens, 0, 1);
    for (int i = 0; i < n_tokens; i++) {
        batch.token[i] = tokens[i];
        batch.pos[i] = i;
        batch.n_seq_id[i] = 1;
        batch.seq_id[i][0] = 0;
        batch.logits[i] = true;
    }
    batch.n_tokens = n_tokens;

    int rc = llama_decode(ctx->ctx, batch);
    free(tokens);

    if (rc != 0) {
        llama_batch_free(batch);
        return -1;
    }

    const float *emb = llama_get_embeddings_seq(ctx->ctx, 0);
    if (!emb) {
        emb = llama_get_embeddings_ith(ctx->ctx, -1);
    }
    if (!emb) {
        emb = llama_get_embeddings(ctx->ctx);
    }

    if (!emb) {
        llama_batch_free(batch);
        return -1;
    }

    memcpy(out_vec, emb, sizeof(float) * (size_t)ctx->dimension);
    vector_normalize_l2(out_vec, ctx->dimension);

    llama_batch_free(batch);
    return 0;
}
