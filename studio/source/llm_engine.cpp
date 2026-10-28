#include "llm_engine.hpp"
#include "mem_tier.h"
#include <arm_neon.h>
#include <stdio.h>
#include <stdlib.h>
#include <malloc.h>
#include <errno.h>
#include <unistd.h>
#include <string.h>
#include <string>
#include <vector>
#include <algorithm>

extern "C" int posix_memalign(void **memptr, size_t alignment, size_t size) {
    if (!memptr) return EINVAL;
    if ((alignment % sizeof(void *)) != 0 || (alignment & (alignment - 1)) != 0) {
        return EINVAL;
    }
    void *ptr = memalign(alignment, size);
    if (!ptr) return ENOMEM;
    *memptr = ptr;
    return 0;
}

extern "C" long sysconf(int name) {
    switch (name) {
        case _SC_PAGESIZE:
            return 4096;
        case _SC_NPROCESSORS_ONLN:
        case _SC_NPROCESSORS_CONF:
            return 3;
        case _SC_PHYS_PAGES:
        case _SC_AVPHYS_PAGES:
            return (3200ULL * 1024 * 1024) / 4096; // 3.2 GB Title Override RAM
        default:
            return 4096;
    }
}

// Fast Q4_0 x Q8_0 dot product on ARM64 Cortex-A57 NEON intrinsics
void LLMEngine::VectorDotQ4_0_Q8_0(int n, float* __restrict s, const void* __restrict vx, const void* __restrict vy) {
    const int qk = 32;
    const int nb = n / qk;

    struct block_q4_0 { uint16_t d; uint8_t qs[16]; };
    struct block_q8_0 { uint16_t d; int8_t  qs[32]; };

    const block_q4_0* __restrict x = (const block_q4_0*)vx;
    const block_q8_0* __restrict y = (const block_q8_0*)vy;

    float32x4_t acc = vdupq_n_f32(0.0f);
    const uint8x16_t m4b = vdupq_n_u8(0x0F);
    const int8x16_t  s8b = vdupq_n_s8(8);

    for (int i = 0; i < nb; ++i) {
        const uint8x16_t v0 = vld1q_u8(x[i].qs);
        const int8x16_t x_lo = vsubq_s8(vreinterpretq_s8_u8(vandq_u8(v0, m4b)), s8b);
        const int8x16_t x_hi = vsubq_s8(vreinterpretq_s8_u8(vshrq_n_u8(v0, 4)), s8b);

        const int8x16_t y_lo = vld1q_s8((const int8_t*)y[i].qs);
        const int8x16_t y_hi = vld1q_s8((const int8_t*)y[i].qs + 16);

        int16x8_t p0 = vmull_s8(vget_low_s8(x_lo),  vget_low_s8(y_lo));
        int16x8_t p1 = vmull_s8(vget_high_s8(x_lo), vget_high_s8(y_lo));
        p0 = vmlal_s8(p0, vget_low_s8(x_hi),  vget_low_s8(y_hi));
        p1 = vmlal_s8(p1, vget_high_s8(x_hi), vget_high_s8(y_hi));

        int32_t sum_i = vaddlvq_s16(vaddq_s16(p0, p1));

        float d_x = vgetq_lane_f32(vcvt_f32_f16( vreinterpret_f16_u16(vdup_n_u16(x[i].d)) ), 0);
        float d_y = vgetq_lane_f32(vcvt_f32_f16( vreinterpret_f16_u16(vdup_n_u16(y[i].d)) ), 0);

        acc = vmlaq_n_f32(acc, vdupq_n_f32(d_x * d_y), (float)sum_i);
    }

    *s = vaddvq_f32(acc);
}

LLMEngine::LLMEngine()
    : m_model(nullptr), m_ctx(nullptr),
      m_generating(false), m_abort_flag(false),
      m_model_loaded(false), m_loaded_path(""),
      m_temperature(0.45f), m_language(0),
      m_stream_cb(nullptr), m_cb_user_data(nullptr) {
    mutexInit(&m_state_mutex);
    utf8_stream_init(&m_utf8_ring);
}

LLMEngine::~LLMEngine() {
    StopGeneration();
    UnloadModel();
}

struct ProgressContext {
    LoadProgressCallback cb;
    void* ud;
};

bool LLMEngine::LoadModelFromSD(const char* sd_path, LoadProgressCallback progress_cb, void* user_data) {
    UnloadModel();

    if (!sd_path || strlen(sd_path) == 0) return false;

    // Check if model file exists
    FILE* fp = fopen(sd_path, "rb");
    if (!fp) {
        return false;
    }
    fclose(fp);

    llama_model_params mparams = llama_model_default_params();
    mparams.load_mode = LLAMA_LOAD_MODE_NONE; // Horizon OS: strictly NO mmap
    mparams.use_extra_bufts = false;
    mparams.n_gpu_layers = 0; // CPU on Cortex-A57

    ProgressContext pctx = { progress_cb, user_data };
    if (progress_cb) {
        mparams.progress_callback = [](float progress, void* ud) -> bool {
            auto* p = static_cast<ProgressContext*>(ud);
            if (p && p->cb) {
                p->cb(progress, p->ud);
            }
            return true;
        };
        mparams.progress_callback_user_data = &pctx;
    }

    m_model = llama_model_load_from_file(sd_path, mparams);
    if (!m_model) {
        return false;
    }

    llama_context_params cparams = llama_context_default_params();
    cparams.n_ctx     = 1024;
    cparams.n_batch   = 128;
    cparams.n_ubatch  = 128;
    cparams.n_threads = 3;   // Utilize 3 Cortex-A57 cores (0, 1, 2)
    cparams.n_threads_batch = 3;
    cparams.abort_callback = [](void* d) -> bool {
        auto* flag = static_cast<volatile bool*>(d);
        return flag ? *flag : false;
    };
    cparams.abort_callback_data = (void*)&m_abort_flag;

    m_ctx = llama_init_from_model(m_model, cparams);
    if (!m_ctx) {
        llama_model_free(m_model);
        m_model = nullptr;
        return false;
    }

    m_model_loaded = true;
    m_loaded_path = sd_path;
    return true;
}

void LLMEngine::UnloadModel() {
    StopGeneration();
    if (m_ctx) {
        llama_free(m_ctx);
        m_ctx = nullptr;
    }
    if (m_model) {
        llama_model_free(m_model);
        m_model = nullptr;
    }
    m_model_loaded = false;
    m_loaded_path = "";
}

bool LLMEngine::StartGenerationAsync(const char* prompt_utf8, TokenStreamCallback stream_cb, void* user_data, float temperature, int language) {
    if (m_generating) return false;

    strncpy(m_prompt_buf, prompt_utf8 ? prompt_utf8 : "", sizeof(m_prompt_buf) - 1);
    m_prompt_buf[sizeof(m_prompt_buf) - 1] = '\0';
    m_stream_cb    = stream_cb;
    m_cb_user_data = user_data;
    m_temperature  = (temperature > 0.05f) ? temperature : 0.45f;
    m_language     = language;
    m_abort_flag   = false;
    m_generating   = true;

    utf8_stream_reset(&m_utf8_ring);

    // 1 MB stack for llama graph evaluation
    Result rc = threadCreate(&m_worker_thread, WorkerThreadFunc, this, nullptr, 1024 * 1024, 0x2C, -2);
    if (R_FAILED(rc)) {
        m_generating = false;
        return false;
    }
    svcSetThreadCoreMask(m_worker_thread.handle, -1, 0x07); // Cores 0, 1, 2
    threadStart(&m_worker_thread);
    return true;
}

void LLMEngine::StopGeneration() {
    if (!m_generating) return;
    m_abort_flag = true;
    threadWaitForExit(&m_worker_thread);
    threadClose(&m_worker_thread);
    m_generating = false;
}

void LLMEngine::WorkerThreadFunc(void* arg) {
    static_cast<LLMEngine*>(arg)->RunInferenceLoop();
}

void LLMEngine::RunInferenceLoop() {
    if (!m_model || !m_ctx) {
        const char* msg = "Модель не загружена в память. Перейдите во вкладку МОДЕЛИ (кнопка [R]) и нажмите [A] на Qwen2.5 для загрузки.";
        char safe_out[256];
        size_t ready_bytes = utf8_stream_push(&m_utf8_ring, msg, strlen(msg), safe_out, sizeof(safe_out));
        if (ready_bytes > 0 && m_stream_cb) {
            safe_out[ready_bytes] = '\0';
            mutexLock(&m_state_mutex);
            m_stream_cb(safe_out, m_cb_user_data);
            mutexUnlock(&m_state_mutex);
        }
        m_generating = false;
        return;
    }

    const struct llama_vocab* vocab = llama_model_get_vocab(m_model);
    if (!vocab) {
        m_generating = false;
        return;
    }

    // ChatML prompt template for Qwen2.5 instruction model
    std::string user_prompt = m_prompt_buf;
    std::string system_msg = (m_language == 1)
        ? "You are a universal, smart and accurate AI assistant SwitchLM running locally on Nintendo Switch. You assist with all topics: programming, math, science, history, languages, general knowledge, writing, and gaming. Answer directly, informatively, and concisely in English."
        : "Ты универсальный, умный и точный AI-ассистент SwitchLM, работающий автономно на консоли Nintendo Switch. Ты помогаешь с любыми темами: программирование, наука, история, математика, языки, общие знания, творчество и игры. Отвечай емко, грамотно и прямо по делу на русском языке.";

    std::string formatted = 
        "<|im_start|>system\n" + system_msg + "<|im_end|>\n" +
        "<|im_start|>user\n" + user_prompt + "<|im_end|>\n" +
        "<|im_start|>assistant\n";

    // 1. Tokenize prompt
    int n_prompt = -llama_tokenize(vocab, formatted.c_str(), (int32_t)formatted.length(), nullptr, 0, true, true);
    if (n_prompt <= 0) n_prompt = 256;
    std::vector<llama_token> prompt_tokens(n_prompt + 32);
    int n_tokens = llama_tokenize(vocab, formatted.c_str(), (int32_t)formatted.length(),
                                  prompt_tokens.data(), (int32_t)prompt_tokens.size(), true, true);
    if (n_tokens <= 0) {
        const char* err_msg = (m_language == 1) ? "[Tokenization Error]" : "[Ошибка токенизации]";
        if (m_stream_cb) {
            mutexLock(&m_state_mutex);
            m_stream_cb(err_msg, m_cb_user_data);
            mutexUnlock(&m_state_mutex);
        }
        m_generating = false;
        return;
    }
    prompt_tokens.resize(n_tokens);

    // 2. Clear KV cache memory before starting new generation
    llama_memory_t mem = llama_get_memory(m_ctx);
    if (mem) {
        llama_memory_clear(mem, true);
    }

    // 3. Initialize sampling chain (Top-K 40, Top-P 0.85, Repetition Penalty 1.15, Temp)
    auto sparams = llama_sampler_chain_default_params();
    llama_sampler* smpl = llama_sampler_chain_init(sparams);
    llama_sampler_chain_add(smpl, llama_sampler_init_top_k(40));
    llama_sampler_chain_add(smpl, llama_sampler_init_top_p(0.85f, 1));
    llama_sampler_chain_add(smpl, llama_sampler_init_penalties(llama_vocab_n_tokens(vocab), 64, 1.15f, 0.0f, 0.0f));
    llama_sampler_chain_add(smpl, llama_sampler_init_temp(m_temperature > 0.05f ? m_temperature : 0.45f));
    llama_sampler_chain_add(smpl, llama_sampler_init_dist(LLAMA_DEFAULT_SEED));

    // 4. Ingest prompt tokens into KV cache in chunks of n_batch (128)
    bool decode_ok = true;
    for (size_t b = 0; b < prompt_tokens.size(); b += 128) {
        if (m_abort_flag) { decode_ok = false; break; }
        int n_eval = (int)std::min((size_t)128, prompt_tokens.size() - b);
        llama_batch batch = llama_batch_get_one(prompt_tokens.data() + b, n_eval);
        if (llama_decode(m_ctx, batch) != 0) {
            decode_ok = false;
            break;
        }
    }

    if (!decode_ok) {
        llama_sampler_free(smpl);
        m_generating = false;
        return;
    }

    // 5. Autoregressive token generation loop
    llama_token cur_token = llama_sampler_sample(smpl, m_ctx, -1);
    llama_sampler_accept(smpl, cur_token);

    const int max_gen_tokens = 512;
    int gen_count = 0;

    while (!m_abort_flag && gen_count < max_gen_tokens && !llama_vocab_is_eog(vocab, cur_token)) {
        char piece[128] = {0};
        int n_chars = llama_token_to_piece(vocab, cur_token, piece, sizeof(piece), 0, false);
        if (n_chars > 0) {
            char safe_out[256];
            size_t ready_bytes = utf8_stream_push(&m_utf8_ring, piece, (size_t)n_chars, safe_out, sizeof(safe_out));
            if (ready_bytes > 0 && m_stream_cb) {
                safe_out[ready_bytes] = '\0';
                mutexLock(&m_state_mutex);
                m_stream_cb(safe_out, m_cb_user_data);
                mutexUnlock(&m_state_mutex);
            }
        }

        llama_batch batch = llama_batch_get_one(&cur_token, 1);
        if (llama_decode(m_ctx, batch) != 0) {
            break;
        }

        cur_token = llama_sampler_sample(smpl, m_ctx, -1);
        llama_sampler_accept(smpl, cur_token);
        gen_count++;
    }

    llama_sampler_free(smpl);
    m_generating = false;
}
