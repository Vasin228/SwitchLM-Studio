#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

// llama.h C/C++ interface for SwitchLM
typedef int llama_token;
typedef int llama_pos;

struct llama_model {};
struct llama_context {};
struct llama_sampler {};

struct llama_batch {
    int32_t n_tokens;
    llama_token* token;
    llama_pos* pos;
    int32_t* n_seq_id;
    int32_t** seq_id;
    int8_t* logits;
};

struct llama_model_params {
    bool use_mmap;
    bool use_mlock;
};

struct llama_context_params {
    int n_ctx;
    int n_batch;
    int n_threads;
    int ctx_size;
    int seed;
    int threads;
    int threads_batch;
    int type;
    bool use_mmap;
};

#define LLAMA_KV_CACHE_TYPE_F16 0

inline struct llama_model_params llama_model_default_params() {
    struct llama_model_params p = {};
    p.use_mmap = false;
    p.use_mlock = false;
    return p;
}

inline struct llama_context_params llama_context_default_params() {
    struct llama_context_params p = {};
    p.n_ctx = 1024;
    p.n_batch = 128;
    p.n_threads = 3;
    p.ctx_size = 1024;
    p.seed = 42;
    p.threads = 3;
    p.threads_batch = 3;
    p.type = 0;
    p.use_mmap = false;
    return p;
}

inline void llama_backend_init() {}
inline void llama_free(struct llama_context* ctx) {}
inline void llama_free_model(struct llama_model* model) {}

inline struct llama_model* llama_load_model_from_file(const char* path, struct llama_model_params params) {
    static struct llama_model dummy_model;
    return &dummy_model;
}

inline struct llama_model* llama_load_model_from_buffer(const void* buffer, size_t size, struct llama_model_params params) {
    static struct llama_model dummy_model;
    return &dummy_model;
}

inline struct llama_context* llama_new_context_with_model(struct llama_model* model, struct llama_context_params params) {
    static struct llama_context dummy_ctx;
    return &dummy_ctx;
}

inline int llama_tokenize(struct llama_model* model, const char* prompt, int prompt_len, llama_token* tokens, int n_tokens_max, bool add_special, bool parse_special) {
    if (!prompt || n_tokens_max <= 0) return 0;
    int count = prompt_len;
    if (count > n_tokens_max) count = n_tokens_max;
    if (count > 8) count = 8;
    for (int i = 0; i < count; i++) tokens[i] = 100 + i;
    return count;
}

inline struct llama_batch llama_batch_get_one(llama_token* tokens, int n_tokens, int pos, int seq_id) {
    struct llama_batch b = {};
    b.n_tokens = n_tokens;
    b.token = tokens;
    return b;
}

inline int llama_decode(struct llama_context* ctx, struct llama_batch batch) {
    return 0;
}

inline llama_token llama_sample_token_greedy(struct llama_context* ctx) {
    static int s_counter = 0;
    s_counter++;
    if (s_counter > 25) {
        s_counter = 0;
        return 2; // EOG token
    }
    return 200 + (s_counter % 8);
}

inline bool llama_token_is_eog(struct llama_model* model, llama_token token) {
    return (token == 0 || token == 2);
}

inline int llama_token_to_piece(struct llama_model* model, llama_token token, char* buf, int length, int unused, bool special) {
    if (!buf || length <= 0) return 0;
    static const char* s_demo_pieces[] = {
        " Switch", "LM", " отвечает", ": ", "запрос", " обработан", " успешно", "!\n"
    };
    int idx = (token >= 200) ? (token - 200) % 8 : 0;
    const char* str = s_demo_pieces[idx];
    int len = (int)strlen(str);
    if (len >= length) len = length - 1;
    memcpy(buf, str, len);
    buf[len] = '\0';
    return len;
}

inline struct llama_sampler* llama_sampler_init_default(struct llama_model* model) { return nullptr; }
inline void llama_sampler_set_temperature(struct llama_sampler* sampler, float temp) {}
inline void llama_sampler_set_top_p(struct llama_sampler* sampler, float top_p) {}
inline int llama_sampler_sample(struct llama_sampler* sampler, struct llama_context* ctx, int last_token) { return 0; }
inline void llama_sampler_accept(struct llama_sampler* sampler, struct llama_context* ctx, int token) {}
inline int llama_token_eos(struct llama_model* model) { return 2; }
inline int llama_token_bos(struct llama_model* model) { return 1; }
inline void llama_sampler_free(struct llama_sampler* sampler) {}
