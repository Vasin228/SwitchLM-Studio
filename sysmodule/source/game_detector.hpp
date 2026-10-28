#pragma once
#include <stdint.h>
#include <stddef.h>
#include <switch.h>

class GameDetector {
public:
    GameDetector();
    ~GameDetector();

    static uint64_t GetRunningTitleID();
    uint64_t getActiveTitleID();
    void getActiveTitleName(char* out_name, size_t max_size);
    const char* getCurrentGameName();

private:
    char m_game_name[128];
    uint64_t m_title_id;
    bool m_initialized;
};
