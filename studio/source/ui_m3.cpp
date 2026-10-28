#define STB_TRUETYPE_IMPLEMENTATION
#include "ui_m3.hpp"
#include <switch.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <cmath>
#include <algorithm>
#include "mem_tier.h"

static M3FontAtlas g_atlas = {};
static bool g_pl_initialized = false;

// Forward declaration
static void draw_rect(uint32_t* fb, uint32_t stride, int x, int y, int w, int h, uint32_t color);
static void draw_rounded_rect(uint32_t* fb, uint32_t stride, int x, int y, int w, int h, int r, uint32_t color);

// Decode next UTF-8 codepoint (handles 1-4 byte UTF-8 including Russian 0xD0/0xD1)
static uint32_t NextUTF8Codepoint(const char** ptr) {
    const uint8_t* s = (const uint8_t*)(*ptr);
    if (*s == 0) return 0;

    if (*s < 0x80) {
        *ptr += 1;
        return *s;
    } else if ((*s & 0xE0) == 0xC0) {
        if ((s[1] & 0xC0) != 0x80) { *ptr += 1; return 0xFFFD; }
        uint32_t cp = ((uint32_t)(s[0] & 0x1F) << 6) | (uint32_t)(s[1] & 0x3F);
        *ptr += 2;
        return cp;
    } else if ((*s & 0xF0) == 0xE0) {
        if ((s[1] & 0xC0) != 0x80 || (s[2] & 0xC0) != 0x80) { *ptr += 1; return 0xFFFD; }
        uint32_t cp = ((uint32_t)(s[0] & 0x0F) << 12) |
                      ((uint32_t)(s[1] & 0x3F) << 6)  |
                      (uint32_t)(s[2] & 0x3F);
        *ptr += 3;
        return cp;
    } else {
        if ((s[1] & 0xC0) != 0x80 || (s[2] & 0xC0) != 0x80 || (s[3] & 0xC0) != 0x80) { *ptr += 1; return 0xFFFD; }
        uint32_t cp = ((uint32_t)(s[0] & 0x07) << 18) |
                      ((uint32_t)(s[1] & 0x3F) << 12) |
                      ((uint32_t)(s[2] & 0x3F) << 6)  |
                      (uint32_t)(s[3] & 0x3F);
        *ptr += 4;
        return cp;
    }
}

// Alpha blend pixel over linear RGBA8888 framebuffer (0xAABBGGRR)
static inline void BlendPixelRGBA8888(uint32_t* fb, uint32_t fb_w, uint32_t fb_h,
                                      int x, int y, uint32_t color, uint8_t alpha) {
    if (x < 0 || y < 0 || (uint32_t)x >= fb_w || (uint32_t)y >= fb_h || alpha == 0) return;

    uint32_t* dst_ptr = &fb[y * fb_w + x];
    if (alpha == 255) {
        *dst_ptr = color;
        return;
    }

    uint32_t dst = *dst_ptr;
    uint32_t sr = (color >>  0) & 0xFF;
    uint32_t sg = (color >>  8) & 0xFF;
    uint32_t sb = (color >> 16) & 0xFF;

    uint32_t dr = (dst >>  0) & 0xFF;
    uint32_t dg = (dst >>  8) & 0xFF;
    uint32_t db = (dst >> 16) & 0xFF;

    uint32_t inv_a = 255 - alpha;
    uint32_t r = (sr * alpha + dr * inv_a) >> 8;
    uint32_t g = (sg * alpha + dg * inv_a) >> 8;
    uint32_t b = (sb * alpha + db * inv_a) >> 8;

    *dst_ptr = (0xFF000000U) | (b << 16) | (g << 8) | r;
}

// Dynamic multi-color shimmer wave calculator for animated glowing focus borders
static inline uint32_t get_shimmer_color(float wave, float phase) {
    float t = 0.5f + 0.5f * wave;
    uint8_t r, g, b;

    if (t < 0.33f) {
        float f = t / 0.33f;
        // Cyan (0x00, 0xE5, 0xFF) -> Electric Lilac (0xD0, 0x70, 0xFF)
        r = (uint8_t)(0x00 * (1.0f - f) + 0xD0 * f);
        g = (uint8_t)(0xE5 * (1.0f - f) + 0x70 * f);
        b = 0xFF;
    } else if (t < 0.66f) {
        float f = (t - 0.33f) / 0.33f;
        // Electric Lilac (0xD0, 0x70, 0xFF) -> Radiant Gold (0xFF, 0xD7, 0x00)
        r = (uint8_t)(0xD0 * (1.0f - f) + 0xFF * f);
        g = (uint8_t)(0x70 * (1.0f - f) + 0xD7 * f);
        b = (uint8_t)(0xFF * (1.0f - f) + 0x00 * f);
    } else {
        float f = (t - 0.66f) / 0.34f;
        // Radiant Gold (0xFF, 0xD7, 0x00) -> Cyan (0x00, 0xE5, 0xFF)
        r = (uint8_t)(0xFF * (1.0f - f) + 0x00 * f);
        g = (uint8_t)(0xD7 * (1.0f - f) + 0xE5 * f);
        b = (uint8_t)(0x00 * (1.0f - f) + 0xFF * f);
    }
    return (0xFF000000U) | ((uint32_t)b << 16) | ((uint32_t)g << 8) | (uint32_t)r;
}

// Draw smoothly rounded RGB shimmering glowing border with 100% continuous SDF perimeter
static void draw_rounded_shimmer_border(uint32_t* fb, uint32_t stride, int x, int y, int w, int h, int r, uint32_t frame_tick) {
    if (!fb || w <= 0 || h <= 0) return;

    if (r < 0) r = 0;
    if (r > w / 2) r = w / 2;
    if (r > h / 2) r = h / 2;

    float phase = (float)frame_tick * 0.05f;

    // Outer rectangle is [x, y] to [x + w - 1, y + h - 1]
    float x_min = (float)x;
    float y_min = (float)y;
    float x_max = (float)(x + w - 1);
    float y_max = (float)(y + h - 1);

    // Inner rectangle where corner arcs center
    float ix0 = x_min + (float)r;
    float ix1 = x_max - (float)r;
    float iy0 = y_min + (float)r;
    float iy1 = y_max - (float)r;

    float len_w = std::max(0.0f, ix1 - ix0);
    float len_h = std::max(0.0f, iy1 - iy0);
    float half_pi = 1.57079633f;
    float arc_len = half_pi * (float)r;
    float P = 2.0f * len_w + 2.0f * len_h + 4.0f * arc_len;
    if (P <= 0.0f) P = 1.0f;
    float freq = 2.0f * 6.2831853f;

    float s_tr_start     = len_w;
    float s_right_start  = s_tr_start + arc_len;
    float s_br_start     = s_right_start + len_h;
    float s_bottom_start = s_br_start + arc_len;
    float s_bl_start     = s_bottom_start + len_w;
    float s_left_start   = s_bl_start + arc_len;
    float s_tl_start     = s_left_start + len_h;

    int min_px = std::max(0, x - 3);
    int max_px = std::min(1279, x + w + 2);
    int min_py = std::max(0, y - 3);
    int max_py = std::min(719, y + h + 2);

    for (int py = min_py; py <= max_py; ++py) {
        float fpy = (float)py;
        for (int px = min_px; px <= max_px; ++px) {
            float fpx = (float)px;

            // Fast skip inside solid center
            if (fpx >= ix0 + 1.0f && fpx <= ix1 - 1.0f && fpy >= iy0 + 1.0f && fpy <= iy1 - 1.0f) {
                continue;
            }

            float qx = std::clamp(fpx, ix0, ix1);
            float qy = std::clamp(fpy, iy0, iy1);
            float dx = fpx - qx;
            float dy = fpy - qy;
            float dist = sqrtf(dx * dx + dy * dy) - (float)r;

            if (dist < -1.5f || dist > 2.5f) continue;

            uint8_t alpha = 0;
            if (dist >= -0.5f && dist <= 0.8f) alpha = 255;
            else if (dist > 0.8f) alpha = (uint8_t)(255.0f * std::clamp((2.5f - dist) / 1.7f, 0.0f, 1.0f));
            else alpha = (uint8_t)(255.0f * std::clamp((dist + 1.5f) / 1.0f, 0.0f, 1.0f));

            float s = 0.0f;
            if (fpx < ix0 && fpy < iy0) {
                float angle = atan2f(-(fpy - iy0), -(fpx - ix0));
                if (angle < 0.0f) angle = 0.0f;
                if (angle > half_pi) angle = half_pi;
                s = s_tl_start + angle * (float)r;
            } else if (fpx > ix1 && fpy < iy0) {
                float angle = atan2f(fpx - ix1, -(fpy - iy0));
                if (angle < 0.0f) angle = 0.0f;
                if (angle > half_pi) angle = half_pi;
                s = s_tr_start + angle * (float)r;
            } else if (fpx > ix1 && fpy > iy1) {
                float angle = atan2f(fpy - iy1, fpx - ix1);
                if (angle < 0.0f) angle = 0.0f;
                if (angle > half_pi) angle = half_pi;
                s = s_br_start + angle * (float)r;
            } else if (fpx < ix0 && fpy > iy1) {
                float angle = atan2f(-(fpx - ix0), fpy - iy1);
                if (angle < 0.0f) angle = 0.0f;
                if (angle > half_pi) angle = half_pi;
                s = s_bl_start + angle * (float)r;
            } else if (fpy <= iy0) {
                s = fpx - ix0;
            } else if (fpx >= ix1) {
                s = s_right_start + (fpy - iy0);
            } else if (fpy >= iy1) {
                s = s_bottom_start + (ix1 - fpx);
            } else {
                s = s_left_start + (iy1 - fpy);
            }

            float t = s / P;
            float wave = sinf(phase * 2.5f + t * freq);
            uint32_t col = get_shimmer_color(wave, phase);
            BlendPixelRGBA8888(fb, stride, 720, px, py, col, alpha);
        }
    }
}

bool UI_M3::InitFont(float pixel_height) {
    if (g_atlas.initialized) return true;

    uint8_t* ttf_buffer = nullptr;
    bool allocated_from_heap = false;

    // 1. Load font from RomFS with 4096-byte alignment
    FILE* f = fopen("romfs:/fonts/NotoSans-Regular.ttf", "rb");
    if (f) {
        fseek(f, 0, SEEK_END);
        size_t fsize = ftell(f);
        fseek(f, 0, SEEK_SET);

        size_t aligned_size = (fsize + 4095ULL) & ~4095ULL;
        ttf_buffer = (uint8_t*)aligned_alloc(4096, aligned_size);
        if (ttf_buffer) {
            fread(ttf_buffer, 1, fsize, f);
            allocated_from_heap = true;
        }
        fclose(f);
    }

    // 2. Fallback to Switch firmware system font if RomFS not mounted
    if (!ttf_buffer) {
        if (R_SUCCEEDED(plInitialize(PlServiceType_User))) {
            g_pl_initialized = true;
            PlFontData font_data;
            if (R_SUCCEEDED(plGetSharedFontByType(&font_data, PlSharedFontType_Standard))) {
                ttf_buffer = (uint8_t*)font_data.address;
            }
        }
    }

    if (!ttf_buffer) return false;

    stbtt_fontinfo info;
    if (!stbtt_InitFont(&info, ttf_buffer, stbtt_GetFontOffsetForIndex(ttf_buffer, 0))) {
        if (allocated_from_heap) free(ttf_buffer);
        return false;
    }

    g_atlas.font_size = pixel_height;
    g_atlas.scale = stbtt_ScaleForPixelHeight(&info, pixel_height);
    int ascent, descent, lineGap;
    stbtt_GetFontVMetrics(&info, &ascent, &descent, &lineGap);
    g_atlas.ascent = ascent * g_atlas.scale;

    stbtt_pack_context spc;
    if (!stbtt_PackBegin(&spc, g_atlas.bitmap, ATLAS_WIDTH, ATLAS_HEIGHT, 0, 1, nullptr)) {
        if (allocated_from_heap) free(ttf_buffer);
        return false;
    }

    stbtt_PackSetOversampling(&spc, 1, 1);

    stbtt_pack_range ranges[2] = {};
    ranges[0].font_size                        = pixel_height;
    ranges[0].first_unicode_codepoint_in_range = ASCII_FIRST;
    ranges[0].num_chars                        = ASCII_COUNT;
    ranges[0].chardata_for_range               = g_atlas.ascii_chars;

    ranges[1].font_size                        = pixel_height;
    ranges[1].first_unicode_codepoint_in_range = CYR_FIRST;
    ranges[1].num_chars                        = CYR_COUNT;
    ranges[1].chardata_for_range               = g_atlas.cyr_chars;

    stbtt_PackFontRanges(&spc, ttf_buffer, 0, ranges, 2);
    stbtt_PackEnd(&spc);

    if (allocated_from_heap) free(ttf_buffer);
    g_atlas.initialized = true;
    return true;
}

void UI_M3::ShutdownFont() {
    if (g_pl_initialized) {
        plExit();
        g_pl_initialized = false;
    }
    g_atlas.initialized = false;
}

float UI_M3::GetTextWidth(const char* utf8_text) {
    if (!g_atlas.initialized || !utf8_text) return 0.0f;
    float w = 0.0f;
    const char* ptr = utf8_text;
    while (*ptr) {
        if (*ptr == '\n') break;
        uint32_t cp = NextUTF8Codepoint(&ptr);
        const stbtt_packedchar* pc = nullptr;
        int char_index = 0;

        if (cp == 0x2500 || cp == 0x2015 || cp == 0x2014 || cp == 0x2013 || cp == 0x2012 || cp == 0x2212 || cp == 0x2022 || cp == 0x25CF || cp == 0xB7) {
            pc = g_atlas.ascii_chars;
            char_index = '-' - ASCII_FIRST;
        } else if (cp == 0xAB || cp == 0xBB || cp == 0x201C || cp == 0x201D) {
            pc = g_atlas.ascii_chars;
            char_index = '"' - ASCII_FIRST;
        } else if (cp == 0x25BA || cp == 0x2192 || cp == 0x3E) {
            pc = g_atlas.ascii_chars;
            char_index = '>' - ASCII_FIRST;
        } else if (cp == 0x25C4 || cp == 0x2190 || cp == 0x3C) {
            pc = g_atlas.ascii_chars;
            char_index = '<' - ASCII_FIRST;
        } else if (cp == 0x25B2 || cp == 0x2191 || cp == 0x5E) {
            pc = g_atlas.ascii_chars;
            char_index = '^' - ASCII_FIRST;
        } else if (cp == 0x25BC || cp == 0x2193 || cp == 0x76) {
            pc = g_atlas.ascii_chars;
            char_index = 'v' - ASCII_FIRST;
        } else if (cp >= ASCII_FIRST && cp < (ASCII_FIRST + ASCII_COUNT)) {
            pc = g_atlas.ascii_chars;
            char_index = cp - ASCII_FIRST;
        } else if (cp >= CYR_FIRST && cp < (CYR_FIRST + CYR_COUNT)) {
            pc = g_atlas.cyr_chars;
            char_index = cp - CYR_FIRST;
        } else {
            pc = g_atlas.ascii_chars;
            char_index = '?' - ASCII_FIRST;
        }

        w += pc[char_index].xadvance;
    }
    return w;
}

float UI_M3::GetTextHeight(const char* utf8_text, int max_width) {
    if (!g_atlas.initialized || !utf8_text) return 0.0f;

    float cur_x = 0.0f;
    float line_height = g_atlas.font_size * 1.35f;
    float cur_y = line_height;

    const char* ptr = utf8_text;
    while (*ptr) {
        if (*ptr == '\n') {
            cur_x = 0.0f;
            cur_y += line_height;
            ptr++;
            continue;
        }

        uint32_t cp = NextUTF8Codepoint(&ptr);
        const stbtt_packedchar* pc = nullptr;
        int char_index = 0;

        if (cp == 0x2500 || cp == 0x2015 || cp == 0x2014 || cp == 0x2013 || cp == 0x2012 || cp == 0x2212 || cp == 0x2022 || cp == 0x25CF || cp == 0xB7) {
            pc = g_atlas.ascii_chars;
            char_index = '-' - ASCII_FIRST;
        } else if (cp == 0xAB || cp == 0xBB || cp == 0x201C || cp == 0x201D) {
            pc = g_atlas.ascii_chars;
            char_index = '"' - ASCII_FIRST;
        } else if (cp == 0x25BA || cp == 0x2192 || cp == 0x3E) {
            pc = g_atlas.ascii_chars;
            char_index = '>' - ASCII_FIRST;
        } else if (cp == 0x25C4 || cp == 0x2190 || cp == 0x3C) {
            pc = g_atlas.ascii_chars;
            char_index = '<' - ASCII_FIRST;
        } else if (cp == 0x25B2 || cp == 0x2191 || cp == 0x5E) {
            pc = g_atlas.ascii_chars;
            char_index = '^' - ASCII_FIRST;
        } else if (cp == 0x25BC || cp == 0x2193 || cp == 0x76) {
            pc = g_atlas.ascii_chars;
            char_index = 'v' - ASCII_FIRST;
        } else if (cp >= ASCII_FIRST && cp < (ASCII_FIRST + ASCII_COUNT)) {
            pc = g_atlas.ascii_chars;
            char_index = cp - ASCII_FIRST;
        } else if (cp >= CYR_FIRST && cp < (CYR_FIRST + CYR_COUNT)) {
            pc = g_atlas.cyr_chars;
            char_index = cp - CYR_FIRST;
        } else {
            pc = g_atlas.ascii_chars;
            char_index = '?' - ASCII_FIRST;
        }

        if (max_width > 0 && (cur_x + pc[char_index].xadvance) > (float)max_width) {
            cur_x = 0.0f;
            cur_y += line_height;
        }
        cur_x += pc[char_index].xadvance;
    }
    return cur_y + line_height;
}

void UI_M3::DrawTextUTF8(uint32_t* fb, uint32_t fb_width, uint32_t fb_height,
                         int x, int y, const char* utf8_text, uint32_t rgba_color,
                         int max_width, int clip_y0, int clip_y1) {
    if (!g_atlas.initialized || !utf8_text || !fb) return;

    bool use_clip = (clip_y1 > clip_y0);
    float cur_x = (float)x;
    float cur_y = (float)y + g_atlas.ascent;
    int start_x = x;
    float line_height = g_atlas.font_size * 1.35f;

    const char* ptr = utf8_text;
    while (*ptr) {
        if (*ptr == '\n') {
            cur_x = (float)start_x;
            cur_y += line_height;
            ptr++;
            continue;
        }

        uint32_t cp = NextUTF8Codepoint(&ptr);
        const stbtt_packedchar* pc = nullptr;
        int char_index = 0;

        if (cp == 0x2500 || cp == 0x2015 || cp == 0x2014 || cp == 0x2013 || cp == 0x2012 || cp == 0x2212 || cp == 0x2022 || cp == 0x25CF || cp == 0xB7) {
            pc = g_atlas.ascii_chars;
            char_index = '-' - ASCII_FIRST;
        } else if (cp == 0xAB || cp == 0xBB || cp == 0x201C || cp == 0x201D) {
            pc = g_atlas.ascii_chars;
            char_index = '"' - ASCII_FIRST;
        } else if (cp == 0x25BA || cp == 0x2192 || cp == 0x3E) {
            pc = g_atlas.ascii_chars;
            char_index = '>' - ASCII_FIRST;
        } else if (cp == 0x25C4 || cp == 0x2190 || cp == 0x3C) {
            pc = g_atlas.ascii_chars;
            char_index = '<' - ASCII_FIRST;
        } else if (cp == 0x25B2 || cp == 0x2191 || cp == 0x5E) {
            pc = g_atlas.ascii_chars;
            char_index = '^' - ASCII_FIRST;
        } else if (cp == 0x25BC || cp == 0x2193 || cp == 0x76) {
            pc = g_atlas.ascii_chars;
            char_index = 'v' - ASCII_FIRST;
        } else if (cp >= ASCII_FIRST && cp < (ASCII_FIRST + ASCII_COUNT)) {
            pc = g_atlas.ascii_chars;
            char_index = cp - ASCII_FIRST;
        } else if (cp >= CYR_FIRST && cp < (CYR_FIRST + CYR_COUNT)) {
            pc = g_atlas.cyr_chars;
            char_index = cp - CYR_FIRST;
        } else {
            pc = g_atlas.ascii_chars;
            char_index = '?' - ASCII_FIRST;
        }

        if (max_width > 0 && (cur_x + pc[char_index].xadvance - start_x) > max_width) {
            cur_x = (float)start_x;
            cur_y += line_height;
        }

        // If whole line is below visible clip area, stop drawing early
        if (use_clip && (cur_y - g_atlas.ascent) >= (float)clip_y1) {
            break;
        }

        // If line is entirely above visible clip area, advance x and continue
        if (use_clip && (cur_y + line_height) <= (float)clip_y0) {
            cur_x += pc[char_index].xadvance;
            continue;
        }

        stbtt_aligned_quad q;
        stbtt_GetPackedQuad(pc, ATLAS_WIDTH, ATLAS_HEIGHT, char_index, &cur_x, &cur_y, &q, 0);

        int x0 = (int)q.x0, y0 = (int)q.y0;
        int x1 = (int)q.x1, y1 = (int)q.y1;
        int u0 = (int)(q.s0 * ATLAS_WIDTH);
        int v0 = (int)(q.t0 * ATLAS_HEIGHT);

        for (int py = y0; py < y1; ++py) {
            if (use_clip && (py < clip_y0 || py >= clip_y1)) continue;
            int atlas_y = v0 + (py - y0);
            if (atlas_y < 0 || atlas_y >= ATLAS_HEIGHT) continue;
            for (int px = x0; px < x1; ++px) {
                int atlas_x = u0 + (px - x0);
                if (atlas_x < 0 || atlas_x >= ATLAS_WIDTH) continue;
                uint8_t alpha = g_atlas.bitmap[atlas_y * ATLAS_WIDTH + atlas_x];
                if (alpha > 0) {
                    BlendPixelRGBA8888(fb, fb_width, fb_height, px, py, rgba_color, alpha);
                }
            }
        }
    }
}

static void draw_rect(uint32_t* fb, uint32_t stride, int x, int y, int w, int h, uint32_t color) {
    int x2 = std::min(x + w, 1280);
    int y2 = std::min(y + h, 720);
    x = std::max(x, 0);
    y = std::max(y, 0);

    for (int py = y; py < y2; py++) {
        for (int px = x; px < x2; px++) {
            fb[py * stride + px] = color;
        }
    }
}

static void draw_rounded_rect(uint32_t* fb, uint32_t stride, int x, int y, int w, int h, int r, uint32_t color) {
    if (!fb || w <= 0 || h <= 0) return;
    if (r < 0) r = 0;
    if (r > w / 2) r = w / 2;
    if (r > h / 2) r = h / 2;

    int x1 = x + w - 1;
    int y1 = y + h - 1;
    int ix0 = x + r;
    int ix1 = x1 - r;
    int iy0 = y + r;
    int iy1 = y1 - r;

    // Central body
    draw_rect(fb, stride, ix0, y, ix1 - ix0 + 1, h, color);
    // Left & Right side strips
    draw_rect(fb, stride, x, iy0, r, iy1 - iy0 + 1, color);
    draw_rect(fb, stride, ix1 + 1, iy0, r, iy1 - iy0 + 1, color);

    // 4 Corner arcs
    int r2 = r * r;
    for (int cy = 0; cy <= r; cy++) {
        for (int cx = 0; cx <= r; cx++) {
            if (cx * cx + cy * cy <= r2) {
                int px_l = ix0 - cx;
                int px_r = ix1 + cx;
                int py_t = iy0 - cy;
                int py_b = iy1 + cy;

                if (px_l >= 0 && px_l < 1280) {
                    if (py_t >= 0 && py_t < 720) fb[py_t * stride + px_l] = color;
                    if (py_b >= 0 && py_b < 720) fb[py_b * stride + px_l] = color;
                }
                if (px_r >= 0 && px_r < 1280) {
                    if (py_t >= 0 && py_t < 720) fb[py_t * stride + px_r] = color;
                    if (py_b >= 0 && py_b < 720) fb[py_b * stride + px_r] = color;
                }
            }
        }
    }
}

void ui_m3_init() {
    UI_M3::InitFont(20.0f);
}

void ui_m3_shutdown() {
    UI_M3::ShutdownFont();
}

void ui_m3_render(uint32_t* framebuf, uint32_t width, uint32_t height, uint32_t stride,
                  int active_tab, bool in_nav_rail, int focused_index, uint32_t frame_tick,
                  const char* user_text, const char* chat_response,
                  float chat_scroll_y,
                  float load_progress,
                  const std::vector<DiscoveredModel>& models,
                  int active_model_idx,
                  const UiM3Settings& settings) {
    if (!framebuf) return;

    // Material 3 Dark Palette (RGBA8888 Little Endian: 0xAABBGGRR)
    uint32_t col_surface           = 0xFF181214; // #141218
    uint32_t col_nav_rail          = 0xFF1E1A1D; // Rail bg
    uint32_t col_container         = 0xFF261F21; // #211F26
    uint32_t col_container_high    = 0xFF30292B; // #2B2930
    uint32_t col_primary           = 0xFFFFBCD0; // #D0BCFF
    uint32_t col_primary_container = 0xFF8B374F; // #4F378B
    uint32_t col_on_primary_cont   = 0xFFFFDDEA; // #EADDFF
    uint32_t col_outline           = 0xFF58444A; // #4A4458
    uint32_t col_text              = 0xFFEDE7ED;
    uint32_t col_text_dim          = 0xFF998F93;
    uint32_t col_accent            = 0xFF87E690; // Light green

    // 1. Background Surface
    draw_rect(framebuf, stride, 0, 0, width, height, col_surface);

    // 2. Navigation Rail (Left, 152px wide)
    int rail_w = 152;
    draw_rect(framebuf, stride, 0, 0, rail_w, height, col_nav_rail);
    draw_rect(framebuf, stride, rail_w - 1, 0, 1, height, col_outline);

    const char* tabs_ru[4] = {"ЧАТ", "МОДЕЛИ", "ОВЕРЛЕЙ", "НАСТРОЙКИ"};
    const char* tabs_en[4] = {"CHAT", "MODELS", "OVERLAY", "SETTINGS"};
    const char** tabs = (settings.language == 1) ? tabs_en : tabs_ru;

    for (int i = 0; i < 4; i++) {
        int item_y = 60 + i * 85;
        bool is_active = (i == active_tab);
        int pill_x = 11, pill_w = 130, pill_h = 44, pill_r = 18;

        float tw = UI_M3::GetTextWidth(tabs[i]);
        int tx = pill_x + (pill_w - (int)tw) / 2;
        int ty = item_y + (pill_h - 20) / 2;

        if (is_active) {
            draw_rounded_rect(framebuf, stride, pill_x, item_y, pill_w, pill_h, pill_r, col_primary_container);
            // Highlight active tab with rounded RGB shimmer only when user is inside nav rail
            if (in_nav_rail) {
                draw_rounded_shimmer_border(framebuf, stride, pill_x, item_y, pill_w, pill_h, pill_r, frame_tick);
            }
            UI_M3::DrawTextUTF8(framebuf, stride, height, tx, ty, tabs[i], col_on_primary_cont);
        } else {
            UI_M3::DrawTextUTF8(framebuf, stride, height, tx, ty, tabs[i], col_text_dim);
        }
    }

    // 3. Header Bar
    int cx = 168, cy = 52, cw = 1084, ch = 612;
    UI_M3::DrawTextUTF8(framebuf, stride, height, cx + 10, 16, "SwitchLM Studio  v1.0  •  by vasin228", col_primary);

    // RAM Compatibility Badge
    MemTierInfo tier = mem_tier_detect();
    char ram_badge[96];
    if (tier.tier == MEM_TIER_1_FULL) {
        snprintf(ram_badge, sizeof(ram_badge), "RAM: %lu MB [TITLE OVERRIDE]", (unsigned long)(tier.free_ram_bytes / (1024 * 1024)));
    } else {
        snprintf(ram_badge, sizeof(ram_badge), "RAM: %lu MB [APPLET MODE]", (unsigned long)(tier.free_ram_bytes / (1024 * 1024)));
    }
    int badge_w = 340, badge_h = 32, badge_r = 16;
    int badge_x = cx + cw - badge_w - 10, badge_y = 10;
    draw_rounded_rect(framebuf, stride, badge_x, badge_y, badge_w, badge_h, badge_r, col_container_high);
    float rb_w = UI_M3::GetTextWidth(ram_badge);
    UI_M3::DrawTextUTF8(framebuf, stride, height, badge_x + (badge_w - (int)rb_w) / 2, badge_y + 6, ram_badge, col_primary);

    // 4. Main Container Card
    draw_rounded_rect(framebuf, stride, cx, cy, cw, ch, 16, col_container);

    if (active_tab == 0) {
        // ========================================================
        // --- TAB 0: CHAT ---
        // ========================================================
        UI_M3::DrawTextUTF8(framebuf, stride, height, cx + 24, cy + 16,
                            (settings.language == 1) ? "AI Assistant (Local Neural Network)" : "AI Ассистент (Локальная русскоязычная LLM)", col_primary);

        // Block 0: Assistant Message Bubble
        int msg_x = cx + 24, msg_y = cy + 46, msg_w = cw - 48, msg_h = 360, msg_r = 16;
        draw_rounded_rect(framebuf, stride, msg_x, msg_y, msg_w, msg_h, msg_r, col_container_high);
        if (!in_nav_rail && focused_index == 0) {
            draw_rounded_shimmer_border(framebuf, stride, msg_x, msg_y, msg_w, msg_h, msg_r, frame_tick);
        }

        int text_area_x = msg_x + 20;
        int text_area_w = msg_w - 44; // Room for scrollbar
        int clip_y0 = msg_y + 14;
        int clip_y1 = msg_y + msg_h - 14;

        const char* text_to_show = (chat_response && strlen(chat_response) > 0) ? chat_response :
            "Добро пожаловать в SwitchLM Studio!\n\n"
            "- Нажмите [ВНИЗ] для перехода к строке ввода текста\n"
            "- Нажмите [ВПРАВО] для перехода к кнопке [A] Отправить\n"
            "- Нажмите [ВЛЕВО] для меню вкладок слева\n"
            "- Нажмите [A] на строке ввода для русской клавиатуры\n"
            "- Правый стик (R-Stick) или [ВВЕРХ/ВНИЗ]: плавная прокрутка ответов\n"
            "- Кнопка [Y]: очистить историю диалога\n\n"
            "Задайте любой вопрос: по коду, истории, науке или играм!";

        // Draw text with scroll offset and strict clipping inside message bubble
        int text_draw_y = clip_y0 - (int)chat_scroll_y;
        UI_M3::DrawTextUTF8(framebuf, stride, height, text_area_x, text_draw_y,
                            text_to_show, col_text, text_area_w, clip_y0, clip_y1);

        // Draw Scrollbar if content exceeds visible area
        float total_text_h = UI_M3::GetTextHeight(text_to_show, text_area_w);
        float visible_h = (float)(clip_y1 - clip_y0);
        if (total_text_h > visible_h) {
            int sb_x = msg_x + msg_w - 14;
            int sb_track_y = clip_y0;
            int sb_track_h = (int)visible_h;
            int sb_w = 4;

            // Track background
            draw_rounded_rect(framebuf, stride, sb_x, sb_track_y, sb_w, sb_track_h, 2, col_surface);

            // Thumb
            float max_scroll = total_text_h - visible_h;
            float thumb_ratio = std::clamp(visible_h / total_text_h, 0.12f, 1.0f);
            int thumb_h = std::max(24, (int)(sb_track_h * thumb_ratio));
            float scroll_ratio = std::clamp(chat_scroll_y / max_scroll, 0.0f, 1.0f);
            int thumb_y = sb_track_y + (int)((sb_track_h - thumb_h) * scroll_ratio);

            uint32_t thumb_col = (!in_nav_rail && focused_index == 0) ? col_primary : col_outline;
            draw_rounded_rect(framebuf, stride, sb_x, thumb_y, sb_w, thumb_h, 2, thumb_col);
        }

        // Row 1: Side-by-side Input Box and Send Button
        int row_y = cy + 420;

        // Block 1: User Input Box (left)
        int inp_x = cx + 24, inp_w = cw - 250, inp_h = 52, inp_r = 16;
        draw_rounded_rect(framebuf, stride, inp_x, row_y, inp_w, inp_h, inp_r, col_container_high);
        if (!in_nav_rail && focused_index == 1) {
            draw_rounded_shimmer_border(framebuf, stride, inp_x, row_y, inp_w, inp_h, inp_r, frame_tick);
        }
        const char* input_placeholder = (settings.language == 1) ? "Press [A] to enter prompt..." : "Нажмите [A] для ввода...";
        const char* input_display = (user_text && strlen(user_text) > 0) ? user_text : input_placeholder;
        uint32_t input_col = (user_text && strlen(user_text) > 0) ? col_text : col_text_dim;
        UI_M3::DrawTextUTF8(framebuf, stride, height, inp_x + 18, row_y + 16, input_display, input_col, inp_w - 36);

        // Block 2: Send Button [A] (right - reached by pressing RIGHT!)
        int btn_x = cx + cw - 210, btn_w = 186, btn_h = 52, btn_r = 16;
        draw_rounded_rect(framebuf, stride, btn_x, row_y, btn_w, btn_h, btn_r, col_primary_container);
        if (!in_nav_rail && focused_index == 2) {
            draw_rounded_shimmer_border(framebuf, stride, btn_x, row_y, btn_w, btn_h, btn_r, frame_tick);
        }
        const char* send_label = (settings.language == 1) ? "[A] Send" : "[A] Отправить";
        float send_w = UI_M3::GetTextWidth(send_label);
        int send_x = btn_x + (btn_w - (int)send_w) / 2;
        int send_y = row_y + (btn_h - 20) / 2;
        UI_M3::DrawTextUTF8(framebuf, stride, height, send_x, send_y, send_label, col_on_primary_cont);

        UI_M3::DrawTextUTF8(framebuf, stride, height, cx + 24, cy + 486,
            (settings.language == 1) ? "R-Stick: scroll  |  [Y] clear  |  [RIGHT] Send  |  [LEFT] menu"
                                     : "R-Стик: прокрутка  |  [Y] очистить  |  [ВПРАВО] Отправить  |  [ВЛЕВО] меню",
            col_text_dim);

    } else if (active_tab == 1) {
        // ========================================================
        // --- TAB 1: MODELS ---
        // ========================================================
        bool is_en = (settings.language == 1);
        UI_M3::DrawTextUTF8(framebuf, stride, height, cx + 24, cy + 16,
                            is_en ? "GGUF Models on SD Card (switch/SwitchLM/models/)" : "GGUF Модели на SD-карте (switch/SwitchLM/models/)", col_primary);

        int start_y = cy + 50;
        int card_h = 92;
        int spacing = 104;

        if (models.empty()) {
            int empty_y = start_y;
            draw_rounded_rect(framebuf, stride, cx + 24, empty_y, cw - 48, card_h, 16, col_container_high);
            UI_M3::DrawTextUTF8(framebuf, stride, height, cx + 45, empty_y + 25,
                                is_en ? "No .gguf model files found on SD card." : "Файлы моделей .gguf не найдены на SD-карте.", col_text);
            UI_M3::DrawTextUTF8(framebuf, stride, height, cx + 45, empty_y + 55,
                                is_en ? "Copy GGUF models into switch/SwitchLM/models/ folder via DBI." : "Скопируйте GGUF-модель в папку switch/SwitchLM/models/ через DBI.", col_text_dim);
        } else {
            int display_count = (int)std::min(models.size(), (size_t)3);
            for (int i = 0; i < display_count; i++) {
                int card_y = start_y + i * spacing;
                draw_rounded_rect(framebuf, stride, cx + 24, card_y, cw - 48, card_h, 16, col_container_high);
                if (!in_nav_rail && focused_index == i) {
                    draw_rounded_shimmer_border(framebuf, stride, cx + 24, card_y, cw - 48, card_h, 16, frame_tick);
                }

                char title[128];
                snprintf(title, sizeof(title), "%d. %s", i + 1, models[i].name.c_str());
                UI_M3::DrawTextUTF8(framebuf, stride, height, cx + 45, card_y + 15, title, col_text);

                char desc[160];
                snprintf(desc, sizeof(desc),
                         is_en ? "Size: %lu MB | Quant: %s | 3.2 GB RAM (Title Override)" : "Размер: %lu MB | Квант: %s | 3.2 ГБ RAM (Title Override)",
                         (unsigned long)(models[i].size_bytes / (1024 * 1024)),
                         models[i].quantization.c_str());
                UI_M3::DrawTextUTF8(framebuf, stride, height, cx + 45, card_y + 45, desc, col_text_dim);

                if (active_model_idx == i) {
                    UI_M3::DrawTextUTF8(framebuf, stride, height, cx + cw - 260, card_y + 15,
                                        is_en ? "[ACTIVE / IN RAM]" : "[АКТИВНА / В RAM]", col_accent);
                } else {
                    UI_M3::DrawTextUTF8(framebuf, stride, height, cx + cw - 260, card_y + 15,
                                        is_en ? "[SELECT - PRESS A]" : "[ВЫБРАТЬ - КНОПКА A]", col_primary);
                }
            }
        }

        if (load_progress >= 0.0f) {
            int prog_y = start_y + ((int)std::min(models.size(), (size_t)2)) * spacing + 10;
            draw_rounded_rect(framebuf, stride, cx + 24, prog_y, cw - 48, 75, 14, col_container_high);
            char prog_text[128];
            snprintf(prog_text, sizeof(prog_text),
                     is_en ? "Loading model weights into RAM: %d%%" : "Загрузка весов модели в память: %d%%",
                     (int)(load_progress * 100.0f));
            UI_M3::DrawTextUTF8(framebuf, stride, height, cx + 45, prog_y + 12, prog_text, col_accent);

            int bar_w = cw - 90;
            draw_rect(framebuf, stride, cx + 45, prog_y + 45, bar_w, 10, col_surface);
            int fill_w = (int)(bar_w * std::clamp(load_progress, 0.0f, 1.0f));
            if (fill_w > 0) {
                draw_rect(framebuf, stride, cx + 45, prog_y + 45, fill_w, 10, col_accent);
            }
        }

    } else if (active_tab == 2) {
        // ========================================================
        // --- TAB 2: OVERLAY & SYSMODULE (COMING SOON) ---
        // ========================================================
        bool is_en = (settings.language == 1);
        UI_M3::DrawTextUTF8(framebuf, stride, height, cx + 24, cy + 16,
                            is_en ? "Tesla In-Game Overlay & Background Sysmodule" : "Внутриигровой оверлей Tesla и системный модуль", col_primary);

        int c0_y = cy + 50, c0_h = 430;
        draw_rounded_rect(framebuf, stride, cx + 24, c0_y, cw - 48, c0_h, 18, col_container_high);
        if (!in_nav_rail && focused_index == 0) {
            draw_rounded_shimmer_border(framebuf, stride, cx + 24, c0_y, cw - 48, c0_h, 18, frame_tick);
        }

        UI_M3::DrawTextUTF8(framebuf, stride, height, cx + 45, c0_y + 24,
                            "[ В РАЗРАБОТКЕ / COMING SOON ]", col_accent);

        UI_M3::DrawTextUTF8(framebuf, stride, height, cx + 45, c0_y + 60,
                            is_en ? "Tesla In-Game Overlay & Sysmodule Daemon" : "Внутриигровой Tesla Overlay & фоновый сервис", col_primary);

        const char* coming_soon_desc = is_en
            ? "The in-game assistant feature set is currently in active development:\n\n"
              "- SwitchLM-Overlay.ovl: summoned directly over any running game with [L + D-Pad Down + R-Stick]\n"
              "- SwitchLM-Sysmodule: background daemon (Title ID: 0100000000004C4D) within strict RAM limits\n"
              "- Zero-RAM Wiki RAG: instant offline walkthroughs, crafting recipes and item database lookup\n"
              "- Fast IPC Bridge: shared-memory token streaming from background worker to HUD\n\n"
              "Currently, development priority is focused on polishing the standalone SwitchLM Studio.\n"
              "Overlay support will be finalized in upcoming releases."
            : "Функционал внутриигрового помощника находится в активной разработке:\n\n"
              "- SwitchLM-Overlay.ovl: оверлей Tesla, вызываемый прямо во время игры по [L + D-Pad Вниз + R-Stick]\n"
              "- SwitchLM-Sysmodule: системный сервис (Title ID: 0100000000004C4D) с безопасным расходом RAM\n"
              "- Zero-RAM Wiki RAG: мгновенный оффлайн-поиск игровых секретов, рецептов крафта и предметов\n"
              "- Быстрый IPC мост: потоковая передача ответов нейросети напрямую в экранный HUD\n\n"
              "Сейчас основной приоритет — автономное приложение SwitchLM Studio.\n"
              "Внутриигровой режим будет доработан в следующих обновлениях.";

        UI_M3::DrawTextUTF8(framebuf, stride, height, cx + 45, c0_y + 105, coming_soon_desc, col_text);

    } else if (active_tab == 3) {
        // ========================================================
        // --- TAB 3: SETTINGS ---
        // ========================================================
        bool is_en = (settings.language == 1);
        UI_M3::DrawTextUTF8(framebuf, stride, height, cx + 24, cy + 14,
                            is_en ? "Inference & Performance Settings" : "Параметры инференса и производительности", col_primary);

        int s_h = 74;

        // Item 0: Language
        int s0_y = cy + 42;
        draw_rounded_rect(framebuf, stride, cx + 24, s0_y, cw - 48, s_h, 16, col_container_high);
        if (!in_nav_rail && focused_index == 0) {
            draw_rounded_shimmer_border(framebuf, stride, cx + 24, s0_y, cw - 48, s_h, 16, frame_tick);
        }
        const char* lang_val = (settings.language == 1) ? "English  [Selected]" : "Русский  [Выбран]";
        char lang_str[128];
        snprintf(lang_str, sizeof(lang_str), "%s: %s",
                 is_en ? "Interface Language" : "Язык интерфейса", lang_val);
        UI_M3::DrawTextUTF8(framebuf, stride, height, cx + 45, s0_y + 14, lang_str, col_accent);
        UI_M3::DrawTextUTF8(framebuf, stride, height, cx + 45, s0_y + 42,
                            is_en ? "Press [A] or [LEFT / RIGHT] to switch language." : "Нажмите [A] или [ВЛЕВО / ВПРАВО] для переключения языка.", col_text_dim);

        // Item 1: Temperature
        int s1_y = cy + 126;
        draw_rounded_rect(framebuf, stride, cx + 24, s1_y, cw - 48, s_h, 16, col_container_high);
        if (!in_nav_rail && focused_index == 1) {
            draw_rounded_shimmer_border(framebuf, stride, cx + 24, s1_y, cw - 48, s_h, 16, frame_tick);
        }
        char temp_str[128];
        snprintf(temp_str, sizeof(temp_str), "%s: %.2f  (%s)",
                 is_en ? "Sampling Temperature" : "Температура сэмплинга", settings.temperature,
                 is_en ? "[LEFT / RIGHT] to adjust" : "Кнопки [ВЛЕВО / ВПРАВО]");
        UI_M3::DrawTextUTF8(framebuf, stride, height, cx + 45, s1_y + 14, temp_str, col_text);
        UI_M3::DrawTextUTF8(framebuf, stride, height, cx + 45, s1_y + 42,
                            is_en ? "Lower = precise & strict; higher = creative & diverse." : "Ниже = точнее и строже; выше = креативнее и разнообразнее.", col_text_dim);

        // Item 2: Context Window
        int s2_y = cy + 210;
        draw_rounded_rect(framebuf, stride, cx + 24, s2_y, cw - 48, s_h, 16, col_container_high);
        if (!in_nav_rail && focused_index == 2) {
            draw_rounded_shimmer_border(framebuf, stride, cx + 24, s2_y, cw - 48, s_h, 16, frame_tick);
        }
        char ctx_str[128];
        snprintf(ctx_str, sizeof(ctx_str), "%s: %d tokens  (%s)",
                 is_en ? "Context Window" : "Контекстное окно", settings.context_size,
                 is_en ? "[LEFT / RIGHT] to cycle" : "Кнопки [ВЛЕВО / ВПРАВО]");
        UI_M3::DrawTextUTF8(framebuf, stride, height, cx + 45, s2_y + 14, ctx_str, col_text);
        UI_M3::DrawTextUTF8(framebuf, stride, height, cx + 45, s2_y + 42,
                            is_en ? "512 (Applet) / 1024 (Fast) / 2048 (Full Title Override)." : "512 (Applet) / 1024 (Быстрый) / 2048 (Полный в Title Override).", col_text_dim);

        // Item 3: CPU Boost
        int s3_y = cy + 294;
        draw_rounded_rect(framebuf, stride, cx + 24, s3_y, cw - 48, s_h, 16, col_container_high);
        if (!in_nav_rail && focused_index == 3) {
            draw_rounded_shimmer_border(framebuf, stride, cx + 24, s3_y, cw - 48, s_h, 16, frame_tick);
        }
        const char* boost_str = is_en
            ? (settings.cpu_boost ? "CPU Boost Clock: 1785 MHz [ENABLED] (Press [A] to toggle)" : "CPU Boost Clock: 1020 MHz [STANDARD] (Press [A] to toggle)")
            : (settings.cpu_boost ? "CPU Boost Clock: 1785 MHz [ВКЛЮЧЕНО] (Кнопка [A] переключает)" : "CPU Boost Clock: 1020 MHz [СТАНДАРТ] (Кнопка [A] переключает)");
        UI_M3::DrawTextUTF8(framebuf, stride, height, cx + 45, s3_y + 14, boost_str, settings.cpu_boost ? col_accent : col_text);
        UI_M3::DrawTextUTF8(framebuf, stride, height, cx + 45, s3_y + 42,
                            is_en ? "Overclocks 3 Cortex-A57 cores during generation." : "Повышает тактовую частоту 3 ядер Cortex-A57 на время генерации текста.", col_text_dim);

        // Project Info & Author Box
        int s4_y = cy + 378;
        draw_rounded_rect(framebuf, stride, cx + 24, s4_y, cw - 48, s_h, 16, col_container_high);
        UI_M3::DrawTextUTF8(framebuf, stride, height, cx + 45, s4_y + 14,
                            is_en ? "Author: vasin228  |  ARM64 NEON Hardware Acceleration [ACTIVE]"
                                  : "Автор проекта: vasin228  |  ARM64 NEON Ускорение [АКТИВНО]", col_accent);
        UI_M3::DrawTextUTF8(framebuf, stride, height, cx + 45, s4_y + 42,
                            is_en ? "Press [Y] on this tab to re-run Setup Wizard • By vasin228"
                                  : "Нажмите [Y] для повтора мастера настройки • Автор: vasin228", col_text_dim);
    }

    // 5. Bottom Navigation Controls Bar (Dynamic contextual hints)
    draw_rect(framebuf, stride, 0, height - 38, width, 38, col_nav_rail);
    draw_rect(framebuf, stride, 0, height - 38, width, 1, col_outline);

    bool is_en = (settings.language == 1);
    char footer_hint[256];
    if (in_nav_rail) {
        snprintf(footer_hint, sizeof(footer_hint),
                 is_en ? "[UP/DOWN] Tabs    [RIGHT / A] Enter Panel    [L / R] Switch Tabs    [+] Exit"
                       : "[ВВЕРХ/ВНИЗ] Вкладки    [ВПРАВО / A] Войти в панель    [L / R] Быстрый выбор    [+] Выход");
    } else if (active_tab == 0) {
        if (focused_index == 0) {
            snprintf(footer_hint, sizeof(footer_hint),
                     is_en ? "[DOWN] Input Bar    [LEFT] Tabs Menu    [B] Stop Generation    [L / R] Tabs    [+] Exit"
                           : "[ВНИЗ] Поле ввода    [ВЛЕВО] Меню вкладок    [B] Стоп генерации    [L / R] Вкладки    [+] Выход");
        } else if (focused_index == 1) {
            snprintf(footer_hint, sizeof(footer_hint),
                     is_en ? "[RIGHT] Send Button    [A / X] Keyboard    [Y] Clear    [LEFT] Tabs Menu    [UP] Chat"
                           : "[ВПРАВО] Кнопка Отправить    [A / X] Клавиатура    [Y] Очистить    [ВЛЕВО] Меню вкладок    [ВВЕРХ] Чат");
        } else {
            snprintf(footer_hint, sizeof(footer_hint),
                     is_en ? "[A] Send Prompt    [LEFT] Input Bar    [UP] Chat    [B] Stop    [+] Exit"
                           : "[A] Отправить запрос    [ВЛЕВО] Поле ввода    [ВВЕРХ] Чат    [B] Стоп    [+] Выход");
        }
    } else if (active_tab == 1) {
        snprintf(footer_hint, sizeof(footer_hint),
                 is_en ? "[UP/DOWN] Select Model    [A] Load into RAM    [LEFT / B] Tabs Menu    [+] Exit"
                       : "[ВВЕРХ/ВНИЗ] Выбор модели    [A] Загрузить модель в RAM    [ВЛЕВО / B] Меню вкладок    [+] Выход");
    } else if (active_tab == 2) {
        snprintf(footer_hint, sizeof(footer_hint),
                 is_en ? "[LEFT / B] Tabs Menu    [L / R] Switch Tabs    [+] Exit"
                       : "[ВЛЕВО / B] Меню вкладок    [L / R] Переключение вкладок    [+] Выход");
    } else {
        snprintf(footer_hint, sizeof(footer_hint),
                 is_en ? "[UP/DOWN] Option    [LEFT / RIGHT] Adjust    [Y] Run Setup Wizard    [+] Exit"
                       : "[ВВЕРХ/ВНИЗ] Параметр    [ВЛЕВО / ВПРАВО] Изменить    [Y] Мастер настройки    [+] Выход");
    }

    UI_M3::DrawTextUTF8(framebuf, stride, height, 130, height - 28, footer_hint, col_text);

    // 6. First-Launch Setup Wizard (Interactive Modal)
    if (settings.first_run) {
        // Fast 75% backdrop dimming
        for (uint32_t py = 0; py < height; ++py) {
            uint32_t* row = &framebuf[py * stride];
            for (uint32_t px = 0; px < width; ++px) {
                row[px] = ((row[px] >> 2) & 0x3F3F3F3F) | 0xFF000000U;
            }
        }

        int mw = 840, mh = 480;
        int mx = ((int)width - mw) / 2;
        int my = ((int)height - mh) / 2;

        // Modal container & glowing border
        draw_rounded_rect(framebuf, stride, mx, my, mw, mh, 24, col_container_high);
        draw_rounded_shimmer_border(framebuf, stride, mx, my, mw, mh, 24, frame_tick);

        if (settings.wizard_step == 0) {
            // STEP 0: LANGUAGE SELECTION
            UI_M3::DrawTextUTF8(framebuf, stride, height, mx + 40, my + 30, "SwitchLM Studio", col_primary);
            UI_M3::DrawTextUTF8(framebuf, stride, height, mx + 40, my + 60, "Первоначальная настройка  /  Initial Setup", col_text);
            UI_M3::DrawTextUTF8(framebuf, stride, height, mx + 40, my + 95, "Выберите язык интерфейса / Select interface language:", col_text_dim);

            int card_w = 360, card_h = 240, card_r = 18;
            int card0_x = mx + 40, card0_y = my + 135;
            int card1_x = mx + 440, card1_y = my + 135;

            // Card 0: Russian
            draw_rounded_rect(framebuf, stride, card0_x, card0_y, card_w, card_h, card_r, col_container);
            if (settings.wizard_focus == 0) {
                draw_rounded_shimmer_border(framebuf, stride, card0_x, card0_y, card_w, card_h, card_r, frame_tick);
            }
            UI_M3::DrawTextUTF8(framebuf, stride, height, card0_x + 25, card0_y + 22, "Русский язык", col_primary);
            UI_M3::DrawTextUTF8(framebuf, stride, height, card0_x + 25, card0_y + 55, "[ Рекомендуется ]", col_accent);
            UI_M3::DrawTextUTF8(framebuf, stride, height, card0_x + 25, card0_y + 95,
                                "- Полная локализация меню\n"
                                "- Экранная клавиатура (ЙЦУКЕН)\n"
                                "- Ответы нейросети на русском\n"
                                "- Подсказки управления геймпадом", col_text_dim);

            // Card 1: English
            draw_rounded_rect(framebuf, stride, card1_x, card1_y, card_w, card_h, card_r, col_container);
            if (settings.wizard_focus == 1) {
                draw_rounded_shimmer_border(framebuf, stride, card1_x, card1_y, card_w, card_h, card_r, frame_tick);
            }
            UI_M3::DrawTextUTF8(framebuf, stride, height, card1_x + 25, card1_y + 22, "English", col_primary);
            UI_M3::DrawTextUTF8(framebuf, stride, height, card1_x + 25, card1_y + 55, "[ Standard ]", col_text_dim);
            UI_M3::DrawTextUTF8(framebuf, stride, height, card1_x + 25, card1_y + 95,
                                "- Full English interface\n"
                                "- QWERTY keyboard layout\n"
                                "- Direct English model prompts\n"
                                "- Gamepad button guidance", col_text_dim);

            // Footer prompt
            UI_M3::DrawTextUTF8(framebuf, stride, height, mx + 40, my + mh - 45,
                                "[ВЛЕВО / ВПРАВО] Выбор языка        [A] Подтвердить и далее", col_text);
        } else {
            // STEP 1: MODEL SELECTION
            bool wiz_en = (settings.language == 1);
            const char* title = wiz_en ? "Step 2: Select Neural Network Model" : "Шаг 2: Выбор нейросетевой модели";
            const char* sub = wiz_en ? "Choose a .gguf model to load into Switch memory:" : "Выберите .gguf модель для загрузки в память Switch:";
            UI_M3::DrawTextUTF8(framebuf, stride, height, mx + 40, my + 30, "SwitchLM Studio", col_primary);
            UI_M3::DrawTextUTF8(framebuf, stride, height, mx + 40, my + 60, title, col_text);
            UI_M3::DrawTextUTF8(framebuf, stride, height, mx + 40, my + 95, sub, col_text_dim);

            if (models.empty()) {
                int empty_y = my + 140;
                draw_rounded_rect(framebuf, stride, mx + 40, empty_y, mw - 80, 100, 16, col_container);
                UI_M3::DrawTextUTF8(framebuf, stride, height, mx + 60, empty_y + 25,
                                    wiz_en ? "No .gguf model files found in switch/SwitchLM/models/." : "Файлы моделей .gguf не найдены на SD-карте.", col_text);
                UI_M3::DrawTextUTF8(framebuf, stride, height, mx + 60, empty_y + 55,
                                    wiz_en ? "Copy GGUF models via DBI and restart the app." : "Скопируйте GGUF-модель в папку switch/SwitchLM/models/ через DBI.", col_text_dim);
            } else {
                int display_cnt = (int)std::min(models.size(), (size_t)3);
                for (int i = 0; i < display_cnt; i++) {
                    int card_y = my + 130 + i * 90;
                    int card_w = mw - 80, card_h = 76, card_r = 16;
                    draw_rounded_rect(framebuf, stride, mx + 40, card_y, card_w, card_h, card_r, col_container);
                    if (settings.wizard_focus == i) {
                        draw_rounded_shimmer_border(framebuf, stride, mx + 40, card_y, card_w, card_h, card_r, frame_tick);
                    }

                    char m_title[128];
                    snprintf(m_title, sizeof(m_title), "%d. %s", i + 1, models[i].name.c_str());
                    UI_M3::DrawTextUTF8(framebuf, stride, height, mx + 60, card_y + 14, m_title, col_text);

                    char m_desc[160];
                    snprintf(m_desc, sizeof(m_desc), "%lu MB | Quant: %s | Title Override OK",
                             (unsigned long)(models[i].size_bytes / (1024 * 1024)),
                             models[i].quantization.c_str());
                    UI_M3::DrawTextUTF8(framebuf, stride, height, mx + 60, card_y + 42, m_desc, col_text_dim);

                    bool is_qwen = (models[i].name.find("Qwen") != std::string::npos || models[i].name.find("qwen") != std::string::npos);
                    if (is_qwen) {
                        UI_M3::DrawTextUTF8(framebuf, stride, height, mx + card_w - 200, card_y + 14,
                                            wiz_en ? "[Recommended]" : "[Рекомендуется]", col_accent);
                    }
                }
            }

            // Footer prompt
            const char* foot = wiz_en
                ? "[UP / DOWN] Select model    [A] Start Chat    [B] Back to Language"
                : "[ВВЕРХ / ВНИЗ] Выбор модели    [A] Запустить чат    [B] Назад к выбору языка";
            UI_M3::DrawTextUTF8(framebuf, stride, height, mx + 40, my + mh - 45, foot, col_text);
        }
    }
}
