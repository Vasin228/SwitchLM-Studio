#pragma once
#include <stdint.h>
#include <stdbool.h>
#include <vector>
#include <string>
#include "stb_truetype.h"
#include "model_manager.hpp"

#define ATLAS_WIDTH  512
#define ATLAS_HEIGHT 512
#define ASCII_FIRST  0x0020
#define ASCII_COUNT  95      // 0x0020 - 0x007E
#define CYR_FIRST    0x0400
#define CYR_COUNT    256     // 0x0400 - 0x04FF

struct M3FontAtlas {
    uint8_t bitmap[ATLAS_WIDTH * ATLAS_HEIGHT];
    stbtt_packedchar ascii_chars[ASCII_COUNT];
    stbtt_packedchar cyr_chars[CYR_COUNT];
    float font_size;
    float ascent;
    float scale;
    bool initialized;
};

namespace UI_M3 {
    bool InitFont(float pixel_height = 20.0f);
    void ShutdownFont();
    float GetTextWidth(const char* utf8_text);
    float GetTextHeight(const char* utf8_text, int max_width);
    void DrawTextUTF8(uint32_t* fb, uint32_t fb_width, uint32_t fb_height,
                      int x, int y, const char* utf8_text, uint32_t rgba_color,
                      int max_width = 0, int clip_y0 = 0, int clip_y1 = 0);
}

struct UiM3Settings {
    float temperature;
    int context_size;
    bool cpu_boost;
    int language;       // 0: Russian, 1: English
    bool first_run;     // whether first launch wizard is shown
    int wizard_step;    // 0: Language, 1: Model
    int wizard_focus;   // focused option in wizard
};

void ui_m3_init();
void ui_m3_shutdown();
void ui_m3_render(uint32_t* framebuf, uint32_t width, uint32_t height, uint32_t stride,
                  int active_tab, bool in_nav_rail, int focused_index, uint32_t frame_tick,
                  const char* user_text, const char* chat_response,
                  float chat_scroll_y,
                  float load_progress,
                  const std::vector<DiscoveredModel>& models,
                  int active_model_idx,
                  const UiM3Settings& settings);
