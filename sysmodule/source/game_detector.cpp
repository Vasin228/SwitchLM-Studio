#include "game_detector.hpp"
#include <switch.h>
#include <string.h>

GameDetector::GameDetector() : m_title_id(0), m_initialized(false) {
    memset(m_game_name, 0, sizeof(m_game_name));
}

GameDetector::~GameDetector() {}

uint64_t GameDetector::GetRunningTitleID() {
    uint64_t pid = 0;
    uint64_t program_id = 0;

    // Get active game process PID safely
    if (R_FAILED(pmdmntGetApplicationProcessId(&pid)) || pid == 0) {
        return 0; // Game not running (qlaunch / hbmenu applet)
    }

    // Convert PID to 64-bit TitleID
    if (R_FAILED(pminfoGetProgramId(&program_id, pid))) {
        return 0;
    }

    return program_id;
}

uint64_t GameDetector::getActiveTitleID() {
    uint64_t tid = GetRunningTitleID();
    if (tid != 0) {
        m_title_id = tid;
    }
    return tid;
}

void GameDetector::getActiveTitleName(char* out_name, size_t max_size) {
    if (m_title_id == 0) {
        getActiveTitleID();
    }
    if (m_title_id == 0) {
        strncpy(out_name, "Unknown", max_size);
        return;
    }

    NsApplicationControlData nacp;
    memset(&nacp, 0, sizeof(nacp));
    size_t out_size = 0;
    Result rc = nsGetApplicationControlData(NsApplicationControlSource_Storage,
        m_title_id, &nacp, sizeof(nacp), &out_size);
    if (R_FAILED(rc)) {
        strncpy(out_name, "Unknown", max_size);
        return;
    }

    NacpLanguageEntry *lang_entry = NULL;
    rc = nacpGetLanguageEntry(&nacp.nacp, &lang_entry);
    if (R_FAILED(rc)) {
        strncpy(out_name, "Unknown", max_size);
        return;
    }

    strncpy(out_name, lang_entry->name, max_size - 1);
    out_name[max_size - 1] = '\0';
    strncpy(m_game_name, out_name, sizeof(m_game_name) - 1);
    m_initialized = true;
}

const char* GameDetector::getCurrentGameName() {
    if (!m_initialized) {
        getActiveTitleName(m_game_name, sizeof(m_game_name));
    }
    return m_game_name;
}
