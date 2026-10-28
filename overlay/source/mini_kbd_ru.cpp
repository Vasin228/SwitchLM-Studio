#include "mini_kbd_ru.hpp"
#include <switch.h>
#include <cstring>
#include <algorithm>

const char* MiniKBDRu::k_chips[4] = {
    "Босс / Тактика",
    "Где найти предмет?",
    "Что даёт этот предмет?",
    "Секреты локации"
};

MiniKBDRu::MiniKBDRu() : m_cursor_x(0), m_cursor_y(0), m_selected_chip(-1) {
    m_text.reserve(256);
    initKeys();
}

MiniKBDRu::~MiniKBDRu() {}

void MiniKBDRu::initKeys() {
    m_keys.clear();
    // 3 rows of 11 characters = 33 Russian letters
    static const char* r1[] = {"А","Б","В","Г","Д","Е","Ё","Ж","З","И","Й"};
    static const char* r2[] = {"К","Л","М","Н","О","П","Р","С","Т","У","Ф"};
    static const char* r3[] = {"Х","Ц","Ч","Ш","Щ","Ъ","Ы","Ь","Э","Ю","Я"};

    s32 key_w = 34;
    s32 key_h = 28;
    s32 gap = 4;
    s32 start_x = 10;
    s32 start_y = 10;

    for (int i = 0; i < 11; i++) {
        m_keys.push_back({r1[i], start_x + i * (key_w + gap), start_y, key_w, key_h});
    }
    start_y += key_h + gap;
    for (int i = 0; i < 11; i++) {
        m_keys.push_back({r2[i], start_x + i * (key_w + gap), start_y, key_w, key_h});
    }
    start_y += key_h + gap;
    for (int i = 0; i < 11; i++) {
        m_keys.push_back({r3[i], start_x + i * (key_w + gap), start_y, key_w, key_h});
    }
    start_y += key_h + gap;

    // Row 4: Space (wide), Backspace, Clear
    m_keys.push_back({"ПРОБЕЛ", start_x, start_y, 180, key_h});
    m_keys.push_back({"<-", start_x + 190, start_y, 80, key_h});
    m_keys.push_back({"ОЧИСТИТЬ", start_x + 280, start_y, 110, key_h});
}

void MiniKBDRu::addChar(const std::string &c) {
    if (m_text.length() < 240) {
        if (c == "ПРОБЕЛ") m_text += " ";
        else m_text += c;
    }
}

void MiniKBDRu::backspace() {
    if (m_text.empty()) return;
    // Remove last UTF-8 character
    while (!m_text.empty()) {
        unsigned char c = (unsigned char)m_text.back();
        m_text.pop_back();
        if ((c & 0xC0) != 0x80) break; // Reached leading byte
    }
}

bool MiniKBDRu::onInput(u64 keys_down, u64 keys_held) {
    bool changed = false;

    if (keys_down & HidNpadButton_Up) {
        if (m_cursor_y > 0) m_cursor_y--;
        changed = true;
    }
    if (keys_down & HidNpadButton_Down) {
        if (m_cursor_y < 3) m_cursor_y++;
        changed = true;
    }
    if (keys_down & HidNpadButton_Left) {
        if (m_cursor_x > 0) m_cursor_x--;
        changed = true;
    }
    if (keys_down & HidNpadButton_Right) {
        int max_x = (m_cursor_y == 3) ? 2 : 10;
        if (m_cursor_x < max_x) m_cursor_x++;
        changed = true;
    }

    if (keys_down & HidNpadButton_A) {
        if (m_cursor_y < 3) {
            int idx = m_cursor_y * 11 + m_cursor_x;
            if (idx >= 0 && idx < (int)m_keys.size()) {
                addChar(m_keys[idx].label);
                changed = true;
            }
        } else {
            // Row 3
            if (m_cursor_x == 0) addChar("ПРОБЕЛ");
            else if (m_cursor_x == 1) backspace();
            else if (m_cursor_x == 2) m_text.clear();
            changed = true;
        }
    }

    if (keys_down & HidNpadButton_Y) {
        backspace();
        changed = true;
    }

    if (keys_down & HidNpadButton_X) {
        m_text.clear();
        changed = true;
    }

    return changed;
}

void MiniKBDRu::drawChip(tsl::gfx::Renderer *renderer, s32 x, s32 y, s32 w, s32 h, const char *text, bool active) {
    tsl::Color bg = active ? tsl::Color{0x4, 0x3, 0x8, 0xF} : tsl::Color{0x2, 0x2, 0x3, 0xF};
    tsl::Color border = active ? tsl::Color{0xD, 0xB, 0xF, 0xF} : tsl::Color{0x4, 0x4, 0x5, 0xF};
    tsl::Color textColor = active ? tsl::Color{0xF, 0xF, 0xF, 0xF} : tsl::Color{0xC, 0xB, 0xE, 0xF};

    renderer->drawRect(x, y, w, h, bg);
    renderer->drawRect(x, y, w, 1, border);
    renderer->drawRect(x, y + h - 1, w, 1, border);
    renderer->drawRect(x, y, 1, h, border);
    renderer->drawRect(x + w - 1, y, 1, h, border);

    renderer->drawString(text, false, x + 8, y + (h - 16) / 2, 16.0f, textColor, w - 16);
}

void MiniKBDRu::render(tsl::gfx::Renderer *renderer, s32 base_x, s32 base_y) {
    for (size_t i = 0; i < m_keys.size(); i++) {
        const auto &k = m_keys[i];
        bool is_selected = false;
        if (i < 33) {
            int row = i / 11;
            int col = i % 11;
            is_selected = (row == m_cursor_y && col == m_cursor_x);
        } else {
            int col = i - 33;
            is_selected = (m_cursor_y == 3 && col == m_cursor_x);
        }

        tsl::Color bg = is_selected ? tsl::Color{0x6, 0x4, 0xA, 0xF} : tsl::Color{0x2, 0x2, 0x2, 0xF};
        tsl::Color border = is_selected ? tsl::Color{0xD, 0xB, 0xF, 0xF} : tsl::Color{0x3, 0x3, 0x4, 0xF};
        tsl::Color txtColor = is_selected ? tsl::Color{0xF, 0xF, 0xF, 0xF} : tsl::Color{0x9, 0x9, 0xA, 0xF};

        s32 kx = base_x + k.x;
        s32 ky = base_y + k.y;

        renderer->drawRect(kx, ky, k.w, k.h, bg);
        if (is_selected) {
            renderer->drawRect(kx, ky, k.w, 2, border);
            renderer->drawRect(kx, ky + k.h - 2, k.w, 2, border);
            renderer->drawRect(kx, ky, 2, k.h, border);
            renderer->drawRect(kx + k.w - 2, ky, 2, k.h, border);
        }

        s32 text_x = kx + (k.w >= 60 ? 8 : (k.w - 14) / 2);
        s32 text_y = ky + (k.h - 16) / 2;
        renderer->drawString(k.label.c_str(), false, text_x, text_y, 15.0f, txtColor, k.w);
    }
}
