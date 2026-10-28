#pragma once
#include <switch.h>
#include "game_detector.hpp"
#include "micro_llm.hpp"
#include "unsafe_mem.hpp"

class IPCServer {
public:
    IPCServer();
    ~IPCServer();

    bool init(GameDetector *detector, MicroLLM *llm);
    void run();
    void stop();

private:
    bool m_running;
    Handle m_port_handle;
    GameDetector *m_detector;
    MicroLLM *m_llm;

    void handleSession(Handle session);
};
