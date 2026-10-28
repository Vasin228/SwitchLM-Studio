#include "micro_llm.hpp"
#include "unsafe_mem.hpp"
#include <arm_neon.h>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

MicroLLM::MicroLLM() : m_model(nullptr), m_ctx(nullptr), m_loaded(false) {}

MicroLLM::~MicroLLM() {
    shutdown();
}

bool MicroLLM::loadModel(const char *path) {
    if (m_loaded) shutdown();

    FILE *f = fopen(path, "rb");
    if (!f) {
        // Fallback check in default directory
        f = fopen("sdmc:/switch/SwitchLM/micro/SwitchLM-GameAssist-135M-Q3_K_M.gguf", "rb");
        if (!f) return false;
    }
    fclose(f);

    struct llama_model_params params = llama_model_default_params();
    params.use_mmap = false; // libnx does not support mmap on sdmc:/
    params.use_mlock = false;

    m_model = llama_load_model_from_file(path, params);
    struct llama_context_params cparams = llama_context_default_params();
    cparams.ctx_size = 512;
    cparams.threads = 1; // Core 3 dedicated
    cparams.use_mmap = false;

    m_ctx = llama_new_context_with_model(m_model, cparams);
    m_loaded = (m_ctx != nullptr);
    return m_loaded;
}

void MicroLLM::shutdown() {
    if (m_ctx) {
        llama_free(m_ctx);
        m_ctx = nullptr;
    }
    if (m_model) {
        llama_free_model(m_model);
        m_model = nullptr;
    }
    m_loaded = false;
}

void MicroLLM::onOverlayInput(u32 input_type, u32 input_value) {
    // Process input chips from overlay
}

// ARM NEON accelerated vector dot product for Q4_0 / Q3_K inference
void matmul_neon_q4(const float* a, const float* b, float* out, int m, int n, int k) {
    for (int i = 0; i < m; i++) {
        for (int j = 0; j < n; j++) {
            float32x4_t sum0 = vdupq_n_f32(0.0f);
            float32x4_t sum1 = vdupq_n_f32(0.0f);
            for (int p = 0; p < k; p += 8) {
                float32x4_t va0 = vld1q_f32(a + i*k + p);
                float32x4_t vb0 = vld1q_f32(b + j*k + p);
                sum0 = vfmaq_f32(sum0, va0, vb0);

                float32x4_t va1 = vld1q_f32(a + i*k + p + 4);
                float32x4_t vb1 = vld1q_f32(b + j*k + p + 4);
                sum1 = vfmaq_f32(sum1, va1, vb1);
            }
            float32x4_t sum = vaddq_f32(sum0, sum1);
            out[i*n + j] = vgetq_lane_f32(sum, 0) + vgetq_lane_f32(sum, 1) +
                           vgetq_lane_f32(sum, 2) + vgetq_lane_f32(sum, 3);
        }
    }
}

// Root Mean Square Normalization with NEON
void rmsnorm_neon(float* x, int n) {
    float32x4_t sum_vec = vdupq_n_f32(0.0f);
    int i = 0;
    for (; i <= n - 4; i += 4) {
        float32x4_t vx = vld1q_f32(x + i);
        sum_vec = vfmaq_f32(sum_vec, vx, vx);
    }
    float sum = vgetq_lane_f32(sum_vec, 0) + vgetq_lane_f32(sum_vec, 1) +
                vgetq_lane_f32(sum_vec, 2) + vgetq_lane_f32(sum_vec, 3);
    for (; i < n; i++) sum += x[i] * x[i];

    float inv_rms = 1.0f / sqrtf(sum / n + 1e-5f);
    float32x4_t vinv = vdupq_n_f32(inv_rms);
    for (i = 0; i <= n - 4; i += 4) {
        float32x4_t vx = vld1q_f32(x + i);
        vst1q_f32(x + i, vmulq_f32(vx, vinv));
    }
    for (; i < n; i++) x[i] *= inv_rms;
}

std::string MicroLLM::infer(const char *prompt, int max_tokens) {
    // Pin worker thread to Core 3 exclusively
    svcSetThreadCoreMask(CUR_THREAD_HANDLE, 0, (1ULL << 3));

    if (!m_loaded) {
        return "Модель не загружена. Проверьте sdmc:/switch/SwitchLM/micro/";
    }

    std::string response;
    response.reserve(512);

    // Yield every 16 iterations to prevent game audio/input stutter
    for (int step = 0; step < max_tokens; step++) {
        if ((step % 16) == 0) {
            svcSleepThread(500000ULL); // 0.5 ms yield
        }

        // Check memory pressure watchdog
        if (UnsafeMem::checkMemoryPressure()) {
            response += "\n[Остановлено: нехватка системной памяти]";
            break;
        }
    }

    return response;
}
