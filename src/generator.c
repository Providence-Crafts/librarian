#include "generator.h"

#include "embedder.h"
#include "llama.h"

#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct generator_context {
    struct llama_model *model;
    struct llama_context *ctx;
    struct llama_sampler *sampler;
    int context_length;
};

generator_context_t *generator_init(const char *model_path, int context_length)
{
    if (!model_path)
        return NULL;

    llama_backend_init();

    struct llama_model_params mparams = llama_model_default_params();
    mparams.n_gpu_layers = 99;
    struct llama_model *model = llama_model_load_from_file(model_path, mparams);
    if (!model) {
        fprintf(stderr, "Failed to load generator model from %s\n", model_path);
        return NULL;
    }

    struct llama_context_params cparams = llama_context_default_params();
    cparams.n_ctx = (context_length > 0) ? (uint32_t)context_length : 4096;
    cparams.n_batch = 512;
    cparams.embeddings = false;

    struct llama_context *ctx = llama_init_from_model(model, cparams);
    if (!ctx) {
        fprintf(stderr, "Failed to initialize generator context\n");
        llama_model_free(model);
        return NULL;
    }

    /* Initialize standard temperature + top-k + top-p sampler chain */
    struct llama_sampler_chain_params sparams = llama_sampler_chain_default_params();
    struct llama_sampler *sampler = llama_sampler_chain_init(sparams);
    llama_sampler_chain_add(sampler, llama_sampler_init_top_k(40));
    llama_sampler_chain_add(sampler, llama_sampler_init_top_p(0.9f, 1));
    llama_sampler_chain_add(sampler, llama_sampler_init_temp(0.7f));
    llama_sampler_chain_add(sampler, llama_sampler_init_dist(1337));

    generator_context_t *gen = calloc(1, sizeof(*gen));
    if (!gen) {
        llama_sampler_free(sampler);
        llama_free(ctx);
        llama_model_free(model);
        return NULL;
    }

    gen->model = model;
    gen->ctx = ctx;
    gen->sampler = sampler;
    gen->context_length = (int)cparams.n_ctx;

    return gen;
}

void generator_free(generator_context_t *ctx)
{
    if (!ctx)
        return;
    if (ctx->sampler) {
        llama_sampler_free(ctx->sampler);
    }
    if (ctx->ctx) {
        llama_free(ctx->ctx);
    }
    if (ctx->model) {
        llama_model_free(ctx->model);
    }
    free(ctx);
}

void generation_result_free(generation_result_t *res)
{
    if (!res)
        return;
    if (res->text) {
        free(res->text);
        res->text = NULL;
    }
}

generation_result_t generator_generate(generator_context_t *ctx, const char *question,
                                       const search_result_t *retrieved, int retrieved_count,
                                       float similarity_threshold, float confidence_threshold)
{
    generation_result_t res;
    memset(&res, 0, sizeof(res));

    /* =========================================================================
     * Stage 1 (Retrieval Refusal)
     * If top similarity S < tau_retrieval, reject immediately without LLM compute.
     * ========================================================================= */
    if (retrieved_count <= 0 || retrieved[0].similarity < similarity_threshold) {
        float top_sim = (retrieved_count > 0) ? retrieved[0].similarity : 0.0f;
        res.is_refusal = true;
        res.confidence = top_sim;
        snprintf(res.refusal_reason, sizeof(res.refusal_reason),
                 "Stage 1 Refusal: Best similarity %.3f < threshold %.3f", (double)top_sim,
                 (double)similarity_threshold);
        res.text = strdup(
            "I do not have sufficient information in my knowledge base to answer this question.");
        return res;
    }

    if (!ctx || !ctx->ctx || !ctx->model) {
        res.is_refusal = true;
        snprintf(res.refusal_reason, sizeof(res.refusal_reason), "Model context uninitialized");
        res.text = strdup("Error: Model context is not initialized.");
        return res;
    }

    /* Assemble RAG prompt with instructions for [INSUFFICIENT_DATA] */
    size_t prompt_cap = 8192;
    char *prompt = malloc(prompt_cap);
    if (!prompt) {
        res.is_refusal = true;
        snprintf(res.refusal_reason, sizeof(res.refusal_reason), "Out of memory");
        res.text = strdup("Error: Out of memory.");
        return res;
    }

    size_t written = (size_t)snprintf(
        prompt, prompt_cap,
        "<|im_start|>system\n"
        "You are an accurate, honest, and direct AI knowledge assistant.\n"
        "Answer the user's question clearly and concisely using ONLY the verified context snippets below.\n"
        "Cite sources using bracketed references like [1], [2] when stating facts from them.\n"
        "If the context does not contain sufficient information to answer the question, or if you are unsure, "
        "you MUST respond ONLY with \"" REFUSAL_INSUFFICIENT_DATA_TOKEN "\".<|im_end|>\n"
        "<|im_start|>user\n"
        "Context:\n");

    for (int i = 0; i < retrieved_count; i++) {
        char chunk_header[600];
        snprintf(chunk_header, sizeof(chunk_header), "[%d] %s\n", i + 1, retrieved[i].doc_path);
        size_t needed = strlen(chunk_header) + strlen(retrieved[i].content) + 4;
        if (written + needed >= prompt_cap) {
            prompt_cap = (written + needed) * 2;
            char *grown = realloc(prompt, prompt_cap);
            if (!grown)
                break;
            prompt = grown;
        }
        written += (size_t)snprintf(prompt + written, prompt_cap - written, "%s%s\n\n",
                                    chunk_header, retrieved[i].content);
    }

    char footer[1024];
    snprintf(footer, sizeof(footer),
             "Question: %s<|im_end|>\n"
             "<|im_start|>assistant\n"
             "<think>\n\n</think>\n\n",
             question);
    if (written + strlen(footer) + 1 >= prompt_cap) {
        prompt_cap = written + strlen(footer) + 256;
        char *grown = realloc(prompt, prompt_cap);
        if (grown)
            prompt = grown;
    }
    snprintf(prompt + written, prompt_cap - written, "%s", footer);

    /* Tokenize prompt */
    utf8_sanitize(prompt);
    const struct llama_vocab *vocab = llama_model_get_vocab(ctx->model);
    int prompt_len = (int)strlen(prompt);
    int n_tokens_alloc = prompt_len + 64;
    llama_token *tokens = malloc(sizeof(llama_token) * (size_t)n_tokens_alloc);
    if (!tokens) {
        free(prompt);
        res.is_refusal = true;
        res.text = strdup("Error: Memory allocation failed during tokenization.");
        return res;
    }

    int n_tokens = llama_tokenize(vocab, prompt, prompt_len, tokens, n_tokens_alloc, true, true);
    if (n_tokens < 0) {
        n_tokens_alloc = -n_tokens;
        llama_token *grown = realloc(tokens, sizeof(llama_token) * (size_t)n_tokens_alloc);
        if (grown) {
            tokens = grown;
            n_tokens =
                llama_tokenize(vocab, prompt, prompt_len, tokens, n_tokens_alloc, true, true);
        }
    }
    free(prompt);

    if (n_tokens <= 0) {
        free(tokens);
        res.is_refusal = true;
        res.text = strdup("Error: Failed to tokenize input prompt.");
        return res;
    }

    /* Truncate tokens if exceeding context length */
    if (n_tokens > ctx->context_length - 128) {
        n_tokens = ctx->context_length - 128;
    }

    /* Clear memory and decode prompt in chunks of n_batch */
    llama_memory_clear(llama_get_memory(ctx->ctx), true);

    int pos = 0;
    int n_batch_sz = 256;
    for (int i = 0; i < n_tokens; i += n_batch_sz) {
        int cur_batch = n_tokens - i;
        if (cur_batch > n_batch_sz)
            cur_batch = n_batch_sz;

        struct llama_batch batch = llama_batch_init(cur_batch, 0, 1);
        for (int b = 0; b < cur_batch; b++) {
            batch.token[b] = tokens[i + b];
            batch.pos[b] = pos++;
            batch.n_seq_id[b] = 1;
            batch.seq_id[b][0] = 0;
            batch.logits[b] = (i + b == n_tokens - 1); /* logits only for the last token */
        }
        batch.n_tokens = cur_batch;

        if (llama_decode(ctx->ctx, batch) != 0) {
            llama_batch_free(batch);
            free(tokens);
            res.is_refusal = true;
            res.text = strdup("Error: Failed to decode prompt into model context.");
            return res;
        }
        llama_batch_free(batch);
    }
    free(tokens);

    /* Autoregressive Generation Loop */
    size_t out_cap = 2048;
    char *out_text = malloc(out_cap);
    if (!out_text) {
        res.is_refusal = true;
        res.text = strdup("Error: Out of memory for generation.");
        return res;
    }
    out_text[0] = '\0';
    size_t out_len = 0;

    double sum_logprobs = 0.0;
    int n_gen_tokens = 0;
    int max_gen_tokens = 256;
    int n_vocab = llama_vocab_n_tokens(vocab);

    llama_sampler_reset(ctx->sampler);

    for (int step = 0; step < max_gen_tokens; step++) {
        llama_token token = llama_sampler_sample(ctx->sampler, ctx->ctx, -1);
        llama_sampler_accept(ctx->sampler, token);

        if (llama_vocab_is_eog(vocab, token)) {
            break;
        }

        /* Calculate token logprob from logits */
        const float *logits = llama_get_logits_ith(ctx->ctx, -1);
        if (logits && n_vocab > 0 && token >= 0 && token < n_vocab) {
            float max_l = -1e9f;
            for (int v = 0; v < n_vocab; v++) {
                if (logits[v] > max_l)
                    max_l = logits[v];
            }
            double sum_exp = 0.0;
            for (int v = 0; v < n_vocab; v++) {
                sum_exp += exp((double)(logits[v] - max_l));
            }
            double log_p = (double)(logits[token] - max_l) - log(sum_exp);
            sum_logprobs += log_p;
            n_gen_tokens++;
        }

        /* Convert token to string piece */
        char piece[128];
        int piece_len = llama_token_to_piece(vocab, token, piece, sizeof(piece), 0, false);
        if (piece_len > 0) {
            if (out_len + (size_t)piece_len + 1 >= out_cap) {
                out_cap = (out_len + (size_t)piece_len + 1) * 2;
                char *grown = realloc(out_text, out_cap);
                if (!grown)
                    break;
                out_text = grown;
            }
            memcpy(out_text + out_len, piece, (size_t)piece_len);
            out_len += (size_t)piece_len;
            out_text[out_len] = '\0';

            /* Check for stop sequences */
            char *stop_pos = NULL;
            if ((stop_pos = strstr(out_text, "<|im_end|>")) != NULL ||
                (stop_pos = strstr(out_text, "<|im_start|>")) != NULL ||
                (stop_pos = strstr(out_text, "\nQuestion:")) != NULL ||
                (stop_pos = strstr(out_text, "\nUser:")) != NULL ||
                (stop_pos = strstr(out_text, "\nHuman:")) != NULL ||
                (stop_pos = strstr(out_text, "\n---")) != NULL) {
                *stop_pos = '\0';
                break;
            }
        }

        /* Forward next token through model */
        struct llama_batch next_batch = llama_batch_init(1, 0, 1);
        next_batch.token[0] = token;
        next_batch.pos[0] = pos++;
        next_batch.n_seq_id[0] = 1;
        next_batch.seq_id[0][0] = 0;
        next_batch.logits[0] = true;
        next_batch.n_tokens = 1;

        int rc = llama_decode(ctx->ctx, next_batch);
        llama_batch_free(next_batch);
        if (rc != 0)
            break;
    }

    /* Strip thinking block if present */
    char *think_end = strstr(out_text, "</think>");
    if (think_end) {
        char *after_think = think_end + 8;
        while (*after_think && isspace((unsigned char)*after_think)) {
            after_think++;
        }
        memmove(out_text, after_think, strlen(after_think) + 1);
    }

    /* Trim trailing whitespace */
    size_t final_len = strlen(out_text);
    while (final_len > 0 && isspace((unsigned char)out_text[final_len - 1])) {
        out_text[--final_len] = '\0';
    }

    /* Compute mean sequence confidence C_gen */
    float mean_conf = 0.0f;
    if (n_gen_tokens > 0) {
        mean_conf = (float)exp(sum_logprobs / (double)n_gen_tokens);
    }
    if (mean_conf < 0.0f)
        mean_conf = 0.0f;
    if (mean_conf > 1.0f)
        mean_conf = 1.0f;

    res.confidence = mean_conf;

    /* =========================================================================
     * Stage 2 (Generation Refusal)
     * If model output contains [INSUFFICIENT_DATA], is empty, OR C_gen < tau_gen, refuse.
     * ========================================================================= */
    if (strstr(out_text, REFUSAL_INSUFFICIENT_DATA_TOKEN) != NULL ||
        mean_conf < confidence_threshold || strlen(out_text) == 0) {
        res.is_refusal = true;
        if (strstr(out_text, REFUSAL_INSUFFICIENT_DATA_TOKEN) != NULL) {
            snprintf(res.refusal_reason, sizeof(res.refusal_reason),
                     "Stage 2 Refusal: Model identified insufficient data in context");
        } else if (strlen(out_text) == 0) {
            snprintf(res.refusal_reason, sizeof(res.refusal_reason),
                     "Stage 2 Refusal: Model produced no grounded response from context");
        } else {
            snprintf(res.refusal_reason, sizeof(res.refusal_reason),
                     "Stage 2 Refusal: Mean token confidence %.2f < threshold %.2f",
                     (double)mean_conf, (double)confidence_threshold);
        }
        free(out_text);
        res.text = strdup("The retrieved knowledge does not contain sufficient verified data to "
                          "answer this question.");
        return res;
    }

    res.is_refusal = false;
    res.text = out_text;
    return res;
}
