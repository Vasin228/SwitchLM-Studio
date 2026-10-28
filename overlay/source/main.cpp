#define TESLA_INIT_IMPL
#include <tesla.hpp>
#include <switch.h>
#include <string>
#include <vector>
#include <cstdio>
#include <cstring>
#include "mini_kbd_ru.hpp"
extern "C" {
#include "wiki_rag.h"
#include "mem_tier.h"
#include "swlm_ipc.h"
}

static char g_rag_snippet_buf[512]; // Static buffer in .bss (0 bytes heap allocations)

// Tesla Assistant GUI for SwitchLM
class GuiAssistant : public tsl::Gui {
public:
    GuiAssistant() : m_game_detected(false), m_active_chip(-1), m_show_kbd(false) {
        memset(m_game_name, 0, sizeof(m_game_name));
        m_title_id = 0;

        // Detect game
        detectCurrentGame();

        // Detect RAM Tier
        MemTier tier = mem_detect_tier();
        if (tier == MEM_TIER_1) {
            m_tier_str = "Tier 1: 300MB+ [0.5B-1.5B]";
        } else if (tier == MEM_TIER_2) {
            m_tier_str = "Tier 2: 62MB [SmolLM2-135M-Q3]";
        } else {
            m_tier_str = "Tier 3: 28MB [25M-Q4]";
        }

        m_response_text = "Выберите вопрос выше или введите запрос с клавиатуры.";
    }

    ~GuiAssistant() {}

    void detectCurrentGame() {
        u64 pid = 0;
        if (R_SUCCEEDED(pmdmntGetApplicationProcessId(&pid)) && pid != 0) {
            if (R_SUCCEEDED(pminfoGetProgramId(&m_title_id, pid)) && m_title_id != 0) {
                m_game_detected = true;
                // Query game name
                NsApplicationControlData nacp;
                size_t out_size = 0;
                if (R_SUCCEEDED(nsGetApplicationControlData(NsApplicationControlSource_Storage, m_title_id, &nacp, sizeof(nacp), &out_size))) {
                    NacpLanguageEntry *lang_entry = nullptr;
                    if (R_SUCCEEDED(nacpGetLanguageEntry(&nacp.nacp, &lang_entry)) && lang_entry) {
                        strncpy(m_game_name, lang_entry->name, sizeof(m_game_name) - 1);
                    }
                }
                if (strlen(m_game_name) == 0) {
                    snprintf(m_game_name, sizeof(m_game_name), "App_%016lX", (unsigned long)m_title_id);
                }
            }
        }

        if (!m_game_detected) {
            strcpy(m_game_name, "Игра не запущена");
        }
    }

    void OnActionChipPressed(const char* query_keyword_utf8) {
        uint64_t active_tid = 0;
        if (R_FAILED(swlmIpcGetActiveTitleId(&active_tid)) || active_tid == 0) {
            active_tid = m_title_id;
        }

        char idx_path[128];
        if (active_tid != 0) {
            snprintf(idx_path, sizeof(idx_path),
                     "sdmc:/switch/SwitchLM/wikis/%016lX.idx", (unsigned long)active_tid);
        } else {
            strncpy(idx_path, "sdmc:/switch/SwitchLM/wikis/global_switch.idx", sizeof(idx_path));
        }

        // Fallback to global_switch.idx if game-specific file does not exist
        FILE* test_fp = fopen(idx_path, "rb");
        if (!test_fp) {
            strncpy(idx_path, "sdmc:/switch/SwitchLM/wikis/global_switch.idx", sizeof(idx_path));
        } else {
            fclose(test_fp);
        }

        // 64-bit FNV-1a hash of lowercase query
        uint64_t target_hash = wiki_fnv1a_64_lower(query_keyword_utf8);

        // Zero-RAM binary lookup directly by file path
        bool found = wiki_rag_lookup_by_hash(idx_path, target_hash,
                                             g_rag_snippet_buf, sizeof(g_rag_snippet_buf));
        if (!found) {
            swlmIpcQueryMicroLLM(active_tid, query_keyword_utf8,
                                 g_rag_snippet_buf, sizeof(g_rag_snippet_buf));
        }

        m_response_text = g_rag_snippet_buf;
    }

    virtual tsl::elm::Element* createUI() override {
        char header_subtitle[128];
        if (m_game_detected) {
            snprintf(header_subtitle, sizeof(header_subtitle), "%s [%016lX]", m_game_name, (unsigned long)m_title_id);
        } else {
            snprintf(header_subtitle, sizeof(header_subtitle), "%s", m_game_name);
        }

        auto frame = new tsl::elm::OverlayFrame("SwitchLM Assistant", header_subtitle);
        auto list = new tsl::elm::List();

        // 1. Memory Tier Header
        list->addItem(new tsl::elm::CategoryHeader(m_tier_str.c_str()));

        // 2. Quick Action Chips (ListItems for easy D-Pad navigation)
        for (int i = 0; i < 4; i++) {
            auto *chipItem = new tsl::elm::ListItem(MiniKBDRu::k_chips[i]);
            int chip_index = i;
            chipItem->setClickListener([this, chip_index](u64 keys) {
                if (keys & HidNpadButton_A) {
                    this->m_active_chip = chip_index;
                    this->OnActionChipPressed(MiniKBDRu::k_chips[chip_index]);
                    return true;
                }
                return false;
            });
            list->addItem(chipItem);
        }

        // 3. Toggle Cyrillic Keyboard
        auto *kbdToggle = new tsl::elm::ListItem("Экранная клавиатура (Русский)");
        kbdToggle->setClickListener([this](u64 keys) {
            if (keys & HidNpadButton_A) {
                this->m_show_kbd = !this->m_show_kbd;
                return true;
            }
            return false;
        });
        list->addItem(kbdToggle);

        // 4. Custom Drawer for keyboard & response view
        list->addItem(new tsl::elm::CustomDrawer([this](tsl::gfx::Renderer *renderer, s32 x, s32 y, s32 w, s32 h) {
            // Draw response box (M3 SurfaceContainer #211F26 -> RGBA4444: 0x2, 0x2, 0x3, 0xF)
            tsl::Color cardBg{0x2, 0x2, 0x3, 0xF};
            tsl::Color cardBorder{0x4, 0x3, 0x8, 0xF};
            tsl::Color textCol{0xE, 0xD, 0xF, 0xF};

            renderer->drawRect(x, y + 5, w, 90, cardBg);
            renderer->drawRect(x, y + 5, w, 2, cardBorder);
            renderer->drawRect(x, y + 93, w, 2, cardBorder);
            renderer->drawString("Ответ ассистента:", false, x + 8, y + 10, 15.0f, tsl::Color{0xD, 0xB, 0xF, 0xF});
            renderer->drawString(this->m_response_text.c_str(), false, x + 8, y + 32, 14.0f, textCol, w - 16);

            // If keyboard is visible, render it
            if (this->m_show_kbd) {
                this->m_kbd.render(renderer, x, y + 105);
            }
        }), 240);

        frame->setContent(list);
        return frame;
    }

    virtual void update() override {}

    virtual bool handleInput(u64 keysDown, u64 keysHeld, const HidTouchState &touchPos, HidAnalogStickState joyStickPosLeft, HidAnalogStickState joyStickPosRight) override {
        if (m_show_kbd) {
            if (m_kbd.onInput(keysDown, keysHeld)) {
                if (keysDown & HidNpadButton_Plus) {
                    // Send query
                    if (!m_kbd.getText().empty()) {
                        OnActionChipPressed(m_kbd.getText().c_str());
                    }
                }
                return true;
            }
        }
        return false;
    }

private:
    bool m_game_detected;
    u64 m_title_id;
    char m_game_name[128];
    std::string m_tier_str;
    std::string m_response_text;
    int m_active_chip;
    bool m_show_kbd;
    MiniKBDRu m_kbd;
};

// SwitchLM Overlay definition
class SwitchLMOverlay : public tsl::Overlay {
public:
    virtual void initServices() override {
        tsl::hlp::doWithSmSession([]() {
            nsInitialize();
            pminfoInitialize();
            pmdmntInitialize();
            swlmIpcInitialize();
        });
    }

    virtual void exitServices() override {
        swlmIpcExit();
        pmdmntExit();
        pminfoExit();
        nsExit();
    }

    virtual void onShow() override {}
    virtual void onHide() override {}

    virtual std::unique_ptr<tsl::Gui> loadInitialGui() override {
        return initially<GuiAssistant>();
    }
};

int main(int argc, char **argv) {
    return tsl::loop<SwitchLMOverlay>(argc, argv);
}
