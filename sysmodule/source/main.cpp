#include <switch.h>
#include <cstdio>
#include "game_detector.hpp"
#include "micro_llm.hpp"
#include "unsafe_mem.hpp"
#include "ipc_server.hpp"

extern "C" {
    u32 __nx_applet_type = AppletType_None;
    u32 __nx_fs_num_sessions = 1;
}

int main(int argc, char* argv[]) {
    // 1. Initialize core system services
    Result rc = smInitialize();
    if (R_FAILED(rc)) return 0;

    rc = fsInitialize();
    if (R_SUCCEEDED(rc)) {
        fsdevMountSdmc();
    }

    pmdmntInitialize();
    pminfoInitialize();
    nsInitialize();

    // 2. Expand memory (+40 MB reserve from SystemUnsafe pool)
    UnsafeMem::expand();

    // 3. Initialize components
    GameDetector detector;
    MicroLLM llm;
    llm.loadModel("sdmc:/switch/SwitchLM/micro/SwitchLM-GameAssist-135M-Q3_K_M.gguf");

    IPCServer ipc;
    if (ipc.init(&detector, &llm)) {
        ipc.run();
    }

    // 4. Clean shutdown
    UnsafeMem::contract();

    nsExit();
    pminfoExit();
    pmdmntExit();
    fsdevUnmountAll();
    fsExit();
    smExit();

    return 0;
}
