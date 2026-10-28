#pragma once
#include <switch.h>
#include <tesla.hpp>
#include <string>
#include <vector>
#include <cstdint>

// Compact Cyrillic keypad for in-game overlay (D-Pad / Touch)
// Avoids calling native Swkbd which conflicts with running games
class MiniKBDRu {
public:
    MiniKBDRu();
    ~MiniKBDRu();

    void render(tsl::gfx::Renderer *renderer, s32 x, s32 y);
    void drawChip(tsl::gfx::Renderer *renderer, s32 x, s32 y, s32 w, s32 h, const char *text, bool active);
    int getSelectedChip() const { return m_selected_chip; }
    void setSelectedChip(int chip) { m_selected_chip = chip; }

    // Handle D-Pad input (returns true if text updated or action triggered)
    bool onInput(u64 keys_down, u64 keys_held);

    // Get current input text (UTF-8 Cyrillic)
    const std::string& getText() const { return m_text; }
    void clearText() { m_text.clear(); }
    void setText(const std::string &txt) { m_text = txt; }

    // Quick gaming prompt chips
    static const char* k_chips[4];

private:
    std::string m_text;
    int m_cursor_x;
    int m_cursor_y;
    int m_selected_chip;

    // Cyrillic key layout
    struct Key {
        std::string label;
        s32 x, y, w, h;
    };
    std::vector<Key> m_keys;

    void initKeys();
    void addChar(const std::string &c);
    void backspace();
};
