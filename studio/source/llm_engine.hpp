#pragma once
#include <switch.h>
#include <stdint.h>
#include <stdbool.h>
#include <string>
#include "llama.h"
#include "utf8_stream.h"

typedef void (*TokenStreamCallback)(const char* safe_utf8_chunk, void* user_data);
typedef void (*LoadProgressCallback)(float progress, void* user_data);

class LLMEngine {
public:
    LLMEngine();
    ~LLMEngine();

    // Model loading via llama_model_load_from_file (no mmap)
    bool LoadModelFromSD(const char* sd_path, LoadProgressCallback progress_cb = nullptr, void* user_data = nullptr);
    void UnloadModel();

    // Async generation pinned to Cores 0..2 with UTF-8 stream protector
    bool StartGenerationAsync(const char* prompt_utf8, TokenStreamCallback stream_cb, void* user_data, float temperature = 0.45f, int language = 0);
    void StopGeneration();
    bool IsGenerating() const { return m_generating; }
    bool IsModelLoaded() const { return m_model_loaded; }
    const std::string& GetLoadedModelPath() const { return m_loaded_path; }

    // Fast dot product on ARM64 Cortex-A57 NEON intrinsics
    static void VectorDotQ4_0_Q8_0(int n, float* __restrict s, const void* __restrict vx, const void* __restrict vy);

private:
    static void WorkerThreadFunc(void* arg);
    void RunInferenceLoop();

    llama_model*   m_model;
    llama_context* m_ctx;
    Utf8Stream     m_utf8_ring;

    Thread   m_worker_thread;
    Mutex    m_state_mutex;
    volatile bool m_generating;
    volatile bool m_abort_flag;
    bool     m_model_loaded;
    std::string m_loaded_path;
    float    m_temperature;
    int      m_language;

    char m_prompt_buf[2048];
    TokenStreamCallback m_stream_cb;
    void* m_cb_user_data;
};
