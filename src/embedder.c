#include "embedder.h"

#include "llama.h"

#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <unistd.h>
#endif

struct embedder_context {
    struct llama_model *model;
    struct llama_context *ctx;
    int dimension;
    int threads;
};

void vector_normalize_l2(float *vec, int dim)
{
    if (!vec || dim <= 0) {
        return;
    }
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

    if (!text || chunk_size_words <= 0) {
        return list;
    }
    if (overlap_words < 0) {
        overlap_words = 0;
    }
    if (overlap_words >= chunk_size_words) {
        overlap_words = chunk_size_words - 1;
    }

    /* First pass: count words and record offsets */
    size_t len = strlen(text);
    if (len == 0) {
        return list;
    }

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
        while (idx < len && isspace((unsigned char)text[idx])) {
            idx++;
        }
        if (idx >= len) {
            break;
        }

        size_t start = idx;
        while (idx < len && !isspace((unsigned char)text[idx])) {
            idx++;
        }
        size_t wlen = idx - start;

        if (word_count >= max_words) {
            max_words *= 2;
            const char **wstarts_new = realloc(word_starts, sizeof(char *) * (size_t)max_words);
            size_t *wlens_new = realloc(word_lens, sizeof(size_t) * (size_t)max_words);
            if (!wstarts_new || !wlens_new) {
                break;
            }
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
    if (step <= 0) {
        step = 1;
    }

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
        if (!chunk_str) {
            break;
        }

        size_t pos = 0;
        for (int i = 0; i < count_in_chunk; i++) {
            if (i > 0) {
                chunk_str[pos++] = ' ';
            }
            memcpy(chunk_str + pos, word_starts[w + i], word_lens[w + i]);
            pos += word_lens[w + i];
        }
        chunk_str[pos] = '\0';

        list.chunks[list.count++] = chunk_str;
        if (w + count_in_chunk >= word_count) {
            break;
        }
    }

    free(word_starts);
    free(word_lens);
    return list;
}

void chunk_list_free(chunk_list_t *list)
{
    if (!list || !list->chunks) {
        return;
    }
    for (int i = 0; i < list->count; i++) {
        free(list->chunks[i]);
    }
    free(list->chunks);
    list->chunks = NULL;
    list->count = 0;
}

static int get_optimal_thread_count(void)
{
    long n = 4;
#ifdef _WIN32
    SYSTEM_INFO sysinfo;
    GetSystemInfo(&sysinfo);
    n = (long)sysinfo.dwNumberOfProcessors;
#else
    n = sysconf(_SC_NPROCESSORS_ONLN);
#endif
    if (n <= 1) {
        return 1;
    }
    if (n <= 4) {
        return (int)(n - 1);
    }
    /* Leave 2 to 4 cores for the OS and background programs */
    int t = (int)(n - 2);
    if (t > 12) {
        t = 12; /* Cap at 12 to maintain L3 cache locality */
    }
    return t;
}

embedder_context_t *embedder_init(const char *model_path, int dimension)
{
    if (!model_path) {
        return NULL;
    }

    llama_backend_init();

    struct llama_model_params mparams = llama_model_default_params();
    mparams.n_gpu_layers = 99;
    struct llama_model *model = llama_model_load_from_file(model_path, mparams);
    if (!model) {
        fprintf(stderr, "Failed to load embedding model from %s\n", model_path);
        return NULL;
    }

    int threads = get_optimal_thread_count();
    struct llama_context_params cparams = llama_context_default_params();
    cparams.embeddings = true;
    cparams.pooling_type = LLAMA_POOLING_TYPE_MEAN;
    cparams.n_ctx = 4096;
    cparams.n_batch = 2048;
    cparams.n_ubatch = 512;
    cparams.n_seq_max = 16;
    cparams.kv_unified = true;
    cparams.n_threads = threads;
    cparams.n_threads_batch = threads;

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
    emb->threads = threads;
    int model_dim = llama_model_n_embd_out(model);
    emb->dimension = (model_dim > 0) ? model_dim : dimension;

    return emb;
}

void embedder_free(embedder_context_t *ctx)
{
    if (!ctx) {
        return;
    }
    if (ctx->ctx) {
        llama_free(ctx->ctx);
    }
    if (ctx->model) {
        llama_model_free(ctx->model);
    }
    free(ctx);
}

int embedder_get_thread_count(const embedder_context_t *ctx)
{
    return ctx ? ctx->threads : 1;
}

void utf8_sanitize(char *str)
{
    if (!str) {
        return;
    }

    unsigned char *s = (unsigned char *)str;
    while (*s) {
        if (*s < 0x80) {
            /* Standard ASCII: replace non-printable ASCII control chars (except \t, \n, \r) with
             * space */
            if (*s < 0x20 && *s != '\t' && *s != '\n' && *s != '\r') {
                *s = ' ';
            }
            s++;
        } else if (*s >= 0xC2 && *s <= 0xDF) {
            /* 2-byte sequence: C2..DF followed by 80..BF */
            if ((s[1] & 0xC0) == 0x80) {
                s += 2;
            } else {
                *s++ = ' ';
            }
        } else if (*s == 0xE0) {
            /* 3-byte sequence: E0 followed by A0..BF, 80..BF */
            if ((s[1] >= 0xA0 && s[1] <= 0xBF) && ((s[2] & 0xC0) == 0x80)) {
                s += 3;
            } else {
                *s++ = ' ';
            }
        } else if ((*s >= 0xE1 && *s <= 0xEC) || (*s >= 0xEE && *s <= 0xEF)) {
            /* 3-byte sequence: E1..EC or EE..EF followed by two 80..BF */
            if (((s[1] & 0xC0) == 0x80) && ((s[2] & 0xC0) == 0x80)) {
                s += 3;
            } else {
                *s++ = ' ';
            }
        } else if (*s == 0xED) {
            /* 3-byte sequence: ED followed by 80..9F, 80..BF (excludes UTF-16 surrogates
             * 0xD800..0xDFFF) */
            if ((s[1] >= 0x80 && s[1] <= 0x9F) && ((s[2] & 0xC0) == 0x80)) {
                s += 3;
            } else {
                *s++ = ' ';
            }
        } else if (*s == 0xF0) {
            /* 4-byte sequence: F0 followed by 90..BF, 80..BF, 80..BF */
            if ((s[1] >= 0x90 && s[1] <= 0xBF) && ((s[2] & 0xC0) == 0x80) &&
                ((s[3] & 0xC0) == 0x80)) {
                s += 4;
            } else {
                *s++ = ' ';
            }
        } else if (*s >= 0xF1 && *s <= 0xF3) {
            /* 4-byte sequence: F1..F3 followed by three 80..BF */
            if (((s[1] & 0xC0) == 0x80) && ((s[2] & 0xC0) == 0x80) && ((s[3] & 0xC0) == 0x80)) {
                s += 4;
            } else {
                *s++ = ' ';
            }
        } else if (*s == 0xF4) {
            /* 4-byte sequence: F4 followed by 80..8F, 80..BF, 80..BF (excludes codepoints >
             * 0x10FFFF) */
            if ((s[1] >= 0x80 && s[1] <= 0x8F) && ((s[2] & 0xC0) == 0x80) &&
                ((s[3] & 0xC0) == 0x80)) {
                s += 4;
            } else {
                *s++ = ' ';
            }
        } else {
            /* Invalid lead byte: 80..C1, F5..FF */
            *s++ = ' ';
        }
    }
}

int embedder_embed_batch(embedder_context_t *ctx, const char *const *texts, float **out_vecs,
                         int count)
{
    if (!ctx || !ctx->ctx || !ctx->model || !texts || !out_vecs || count <= 0) {
        return -1;
    }

    const struct llama_vocab *vocab = llama_model_get_vocab(ctx->model);

    typedef struct {
        llama_token *tokens;
        int n_tokens;
    } token_seq_t;

    token_seq_t *seqs = calloc((size_t)count, sizeof(token_seq_t));
    if (!seqs) {
        return -1;
    }

    for (int i = 0; i < count; i++) {
        const char *t = texts[i];
        if (!t || t[0] == '\0') {
            seqs[i].tokens = NULL;
            seqs[i].n_tokens = 0;
            if (out_vecs[i]) {
                memset(out_vecs[i], 0, sizeof(float) * (size_t)ctx->dimension);
            }
            continue;
        }

        char *clean_t = strdup(t);
        if (!clean_t) {
            continue;
        }
        utf8_sanitize(clean_t);

        int text_len = (int)strlen(clean_t);
        int alloc_tokens = text_len + 32;
        if (alloc_tokens < 64) {
            alloc_tokens = 64;
        }

        llama_token *toks = malloc(sizeof(llama_token) * (size_t)alloc_tokens);
        if (!toks) {
            free(clean_t);
            continue;
        }

        int n = llama_tokenize(vocab, clean_t, text_len, toks, alloc_tokens, true, true);
        if (n < 0) {
            alloc_tokens = -n;
            llama_token *grown = realloc(toks, sizeof(llama_token) * (size_t)alloc_tokens);
            if (grown) {
                toks = grown;
                n = llama_tokenize(vocab, clean_t, text_len, toks, alloc_tokens, true, true);
            }
        }
        free(clean_t);

        if (n > 0) {
            seqs[i].tokens = toks;
            seqs[i].n_tokens = n;
        } else {
            free(toks);
            seqs[i].tokens = NULL;
            seqs[i].n_tokens = 0;
            if (out_vecs[i]) {
                memset(out_vecs[i], 0, sizeof(float) * (size_t)ctx->dimension);
            }
        }
    }

    /* Process sequences in parallel batches up to 2048 tokens and 16 sequences */
    int seq_start = 0;
    while (seq_start < count) {
        int seq_count = 0;
        int total_tokens = 0;

        while (seq_start + seq_count < count && seq_count < 16) {
            int n_tok = seqs[seq_start + seq_count].n_tokens;
            if (n_tok <= 0) {
                seq_count++;
                continue;
            }
            if (n_tok > 2000) {
                n_tok = 2000;
                seqs[seq_start + seq_count].n_tokens = 2000;
            }
            if (total_tokens + n_tok > 2048 && seq_count > 0) {
                break;
            }
            total_tokens += n_tok;
            seq_count++;
        }

        if (total_tokens == 0) {
            for (int s = 0; s < seq_count; s++) {
                int seq_idx = seq_start + s;
                if (out_vecs[seq_idx]) {
                    memset(out_vecs[seq_idx], 0, sizeof(float) * (size_t)ctx->dimension);
                }
            }
            seq_start += seq_count;
            continue;
        }

        llama_memory_clear(llama_get_memory(ctx->ctx), true);

        struct llama_batch batch = llama_batch_init(total_tokens, 0, seq_count);
        int tok_idx = 0;
        for (int s = 0; s < seq_count; s++) {
            int seq_idx = seq_start + s;
            int n_tok = seqs[seq_idx].n_tokens;
            if (n_tok <= 0) {
                continue;
            }
            for (int t = 0; t < n_tok; t++) {
                batch.token[tok_idx] = seqs[seq_idx].tokens[t];
                batch.pos[tok_idx] = t;
                batch.n_seq_id[tok_idx] = 1;
                batch.seq_id[tok_idx][0] = s;
                batch.logits[tok_idx] = true;
                tok_idx++;
            }
        }
        batch.n_tokens = tok_idx;

        int rc = llama_decode(ctx->ctx, batch);
        if (rc == 0) {
            for (int s = 0; s < seq_count; s++) {
                int seq_idx = seq_start + s;
                if (!out_vecs[seq_idx]) {
                    continue;
                }
                if (seqs[seq_idx].n_tokens <= 0) {
                    memset(out_vecs[seq_idx], 0, sizeof(float) * (size_t)ctx->dimension);
                    continue;
                }
                const float *emb = llama_get_embeddings_seq(ctx->ctx, s);
                if (emb) {
                    memcpy(out_vecs[seq_idx], emb, sizeof(float) * (size_t)ctx->dimension);
                    vector_normalize_l2(out_vecs[seq_idx], ctx->dimension);
                } else {
                    memset(out_vecs[seq_idx], 0, sizeof(float) * (size_t)ctx->dimension);
                }
            }
        } else {
            for (int s = 0; s < seq_count; s++) {
                int seq_idx = seq_start + s;
                if (out_vecs[seq_idx]) {
                    memset(out_vecs[seq_idx], 0, sizeof(float) * (size_t)ctx->dimension);
                }
            }
        }
        llama_batch_free(batch);
        seq_start += seq_count;
    }

    for (int i = 0; i < count; i++) {
        free(seqs[i].tokens);
    }
    free(seqs);

    return 0;
}

int embedder_embed(embedder_context_t *ctx, const char *text, float *out_vec)
{
    return embedder_embed_batch(ctx, &text, &out_vec, 1);
}
