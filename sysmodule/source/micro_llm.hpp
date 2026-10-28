#pragma once
#include <string>
#include <cstdint>
#include <switch.h>

extern "C" {
#include <llama.h>
}

class MicroLLM {
public:
    MicroLLM();
    ~MicroLLM();

    bool loadModel(const char *path);
    void shutdown();

    // Run inference on a game-assist prompt
    std::string infer(const char *prompt, int max_tokens = 256);

    // Handle input from overlay (chip selection, etc.)
    void onOverlayInput(u32 input_type, u32 input_value);

    bool isLoaded() const { return m_loaded; }

private:
    llama_model *m_model;
    llama_context *m_ctx;
    bool m_loaded;
};
