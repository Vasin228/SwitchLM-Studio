#include <switch.h>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include <algorithm>
#include <sys/stat.h>
#include "swkbd_ru.hpp"
#include "ui_m3.hpp"
#include "llm_engine.hpp"
#include "model_manager.hpp"
#include "mem_tier.h"
#include "swlm_ipc.h"

static const char* CONFIG_PATH = "sdmc:/switch/SwitchLM/config.ini";

static const char* WELCOME_TEXT_RU =
    "SwitchLM Studio готова к работе!\n\n"
    "Модель активна в оперативной памяти Switch (3.2 ГБ Title Override).\n\n"
    "- Нажмите [ВНИЗ] для перехода к строке ввода текста\n"
    "- Нажмите [ВПРАВО] для перехода к кнопке [A] Отправить\n"
    "- Нажмите [ВЛЕВО] для перехода в меню вкладок слева\n"
    "- Нажмите [A] на строке ввода для экранной клавиатуры\n"
    "- Правый стик (R-Stick) или [ВВЕРХ/ВНИЗ]: плавная прокрутка ответов\n"
    "- Кнопка [Y]: очистить историю диалога\n\n"
    "Задайте любой вопрос: по коду, истории, науке или играм!";

static const char* WELCOME_TEXT_EN =
    "SwitchLM Studio is ready!\n\n"
    "Neural model is active in Switch RAM (3.2 GB Title Override).\n\n"
    "- Press [DOWN] to navigate to the text input bar\n"
    "- Press [RIGHT] to navigate to the [A] Send button\n"
    "- Press [LEFT] to access the side navigation tabs\n"
    "- Press [A] on the input bar to open on-screen keyboard\n"
    "- Right Stick (R-Stick) or [UP/DOWN]: smooth response scrolling\n"
    "- Press [Y] to clear dialogue history\n\n"
    "Ask any question: coding, science, history, or gaming tips!";

static std::string g_chat_response = WELCOME_TEXT_RU;
static Mutex g_chat_mutex;
static float g_chat_scroll_y = 0.0f;
static bool  g_auto_scroll = true;
static float g_load_progress = -1.0f;
static LLMEngine g_engine;
static ModelManager g_model_mgr;
static UiM3Settings g_settings = { 0.45f, 1024, true, 0, false, 0, 0 };

static void SaveConfig(const UiM3Settings& s, const std::string& model_path) {
    FILE* f = fopen(CONFIG_PATH, "w");
    if (!f) return;
    fprintf(f, "[SwitchLM]\n");
    fprintf(f, "first_run=%d\n", s.first_run ? 1 : 0);
    fprintf(f, "language=%d\n", s.language);
    fprintf(f, "temperature=%.2f\n", s.temperature);
    fprintf(f, "context_size=%d\n", s.context_size);
    fprintf(f, "cpu_boost=%d\n", s.cpu_boost ? 1 : 0);
    fprintf(f, "model_path=%s\n", model_path.c_str());
    fclose(f);
}

static bool LoadConfig(UiM3Settings& s, std::string& model_path) {
    FILE* f = fopen(CONFIG_PATH, "r");
    if (!f) return false;
    char line[256];
    bool loaded_any = false;
    while (fgets(line, sizeof(line), f)) {
        char key[64], val[192];
        if (sscanf(line, "%63[^=]=%191s", key, val) == 2) {
            loaded_any = true;
            if (strcmp(key, "first_run") == 0) {
                s.first_run = (atoi(val) != 0);
            } else if (strcmp(key, "language") == 0) {
                s.language = atoi(val);
            } else if (strcmp(key, "temperature") == 0) {
                s.temperature = (float)atof(val);
            } else if (strcmp(key, "context_size") == 0) {
                s.context_size = atoi(val);
            } else if (strcmp(key, "cpu_boost") == 0) {
                s.cpu_boost = (atoi(val) != 0);
            } else if (strcmp(key, "model_path") == 0) {
                model_path = val;
            }
        }
    }
    fclose(f);
    return loaded_any;
}

static void OnTokenReceived(const char* safe_utf8_chunk, void* user_data) {
    if (safe_utf8_chunk) {
        mutexLock(&g_chat_mutex);
        g_chat_response.append(safe_utf8_chunk);
        mutexUnlock(&g_chat_mutex);
    }
}

static void OnLoadProgress(float progress, void* user_data) {
    g_load_progress = progress;
}

int main(int argc, char* argv[]) {
    // 1. Initialize threading synchronization
    mutexInit(&g_chat_mutex);

    // 2. Mount RomFS for TTF fonts
    bool romfs_mounted = false;
    if (R_SUCCEEDED(romfsInit())) {
        romfs_mounted = true;
    }

    // 3. Ensure models directories exist and scan SD card
    mkdir("sdmc:/switch", 0777);
    mkdir("sdmc:/switch/SwitchLM", 0777);
    mkdir("sdmc:/switch/SwitchLM/models", 0777);
    mkdir("sdmc:/switch/SwitchLM/micro", 0777);
    mkdir("sdmc:/switch/SwitchLM/wikis", 0777);

    g_model_mgr.scanModels();

    // 4. Load persistent configuration or trigger first-launch wizard
    std::string saved_model_path = "";
    bool config_exists = LoadConfig(g_settings, saved_model_path);
    if (!config_exists) {
        g_settings.first_run = true;
        g_settings.wizard_step = 0;
        g_settings.wizard_focus = 0;
        g_settings.language = 0;
        g_settings.temperature = 0.45f;
        g_settings.context_size = 1024;
        g_settings.cpu_boost = true;
    }

    g_chat_response = (g_settings.language == 1) ? WELCOME_TEXT_EN : WELCOME_TEXT_RU;

    // 5. Initialize gamepad input
    padConfigureInput(1, HidNpadStyleSet_NpadStandard);
    PadState pad;
    padInitializeDefault(&pad);

    // 6. Initialize Linear Framebuffer (1280x720 RGBA8888)
    NWindow* win = nwindowGetDefault();
    Framebuffer fb;
    memset(&fb, 0, sizeof(fb));
    Result rc = framebufferCreate(&fb, win, 1280, 720, PIXEL_FORMAT_RGBA_8888, 2);
    if (R_FAILED(rc)) {
        consoleInit(NULL);
        printf("SwitchLM Studio - Framebuffer init error: 0x%x\n", rc);
        printf("Press + to exit.\n");
        while (appletMainLoop()) {
            padUpdate(&pad);
            if (padGetButtonsDown(&pad) & HidNpadButton_Plus) break;
            consoleUpdate(NULL);
        }
        consoleExit(NULL);
        if (romfs_mounted) romfsExit();
        return 0;
    }

    framebufferMakeLinear(&fb);
    ui_m3_init();

    int active_tab = 0;       // 0: Chat, 1: Models, 2: Overlay, 3: Settings
    bool in_nav_rail = false; // Whether focus is currently in the left tabs rail
    int focused_index = 1;    // In Tab 0: 0=Message, 1=Input Box, 2=Send Button
    uint32_t frame_tick = 0;
    int active_loaded_model_idx = -1;

    char user_input[256] = "";

    // If config exists and not in wizard, auto-load saved model or first discovered model
    if (!g_settings.first_run) {
        std::string load_target = saved_model_path;
        if (load_target.empty() && g_model_mgr.getModelCount() > 0) {
            const DiscoveredModel* m = g_model_mgr.getModel(0);
            if (m) load_target = m->path;
        }
        if (!load_target.empty()) {
            if (g_engine.LoadModelFromSD(load_target.c_str(), OnLoadProgress, nullptr)) {
                for (size_t i = 0; i < g_model_mgr.getModelCount(); ++i) {
                    if (g_model_mgr.getModel(i)->path == load_target) {
                        active_loaded_model_idx = (int)i;
                        break;
                    }
                }
            }
            g_load_progress = -1.0f;
        }
    }

    // Main Applet Loop
    while (appletMainLoop()) {
        padUpdate(&pad);
        u64 kDown = padGetButtonsDown(&pad);

        // Exit on +
        if (kDown & HidNpadButton_Plus) break;

        // ========================================================
        // 0. FIRST-LAUNCH SETUP WIZARD INPUT
        // ========================================================
        if (g_settings.first_run) {
            if (g_settings.wizard_step == 0) {
                // Step 0: Language selection (0: RU, 1: EN)
                if (kDown & (HidNpadButton_Left | HidNpadButton_StickLLeft)) {
                    g_settings.wizard_focus = 0;
                    g_settings.language = 0;
                }
                if (kDown & (HidNpadButton_Right | HidNpadButton_StickLRight)) {
                    g_settings.wizard_focus = 1;
                    g_settings.language = 1;
                }
                if (kDown & HidNpadButton_A) {
                    g_settings.language = g_settings.wizard_focus;
                    g_settings.wizard_step = 1;
                    g_settings.wizard_focus = 0;
                }
            } else if (g_settings.wizard_step == 1) {
                // Step 1: Model selection
                int count = (int)g_model_mgr.getModelCount();
                int max_focus = (count > 0) ? std::min(count - 1, 2) : 0;
                if (kDown & (HidNpadButton_Up | HidNpadButton_StickLUp)) {
                    if (g_settings.wizard_focus > 0) g_settings.wizard_focus--;
                }
                if (kDown & (HidNpadButton_Down | HidNpadButton_StickLDown)) {
                    if (g_settings.wizard_focus < max_focus) g_settings.wizard_focus++;
                }
                if (kDown & HidNpadButton_B) {
                    g_settings.wizard_step = 0;
                    g_settings.wizard_focus = g_settings.language;
                }
                if (kDown & HidNpadButton_A) {
                    std::string chosen_path = "";
                    if (count > 0 && g_settings.wizard_focus < count) {
                        const DiscoveredModel* m = g_model_mgr.getModel(g_settings.wizard_focus);
                        if (m) {
                            chosen_path = m->path;
                            active_loaded_model_idx = g_settings.wizard_focus;
                            g_engine.LoadModelFromSD(chosen_path.c_str(), OnLoadProgress, nullptr);
                        }
                    }
                    g_settings.first_run = false;
                    SaveConfig(g_settings, chosen_path);

                    mutexLock(&g_chat_mutex);
                    g_chat_response = (g_settings.language == 1) ? WELCOME_TEXT_EN : WELCOME_TEXT_RU;
                    mutexUnlock(&g_chat_mutex);

                    active_tab = 0;
                    in_nav_rail = false;
                    focused_index = 1;
                    g_load_progress = -1.0f;
                }
            }
        }
        // ========================================================
        // REGULAR APPLICATION NAVIGATION (Wizard inactive)
        // ========================================================
        else {
            // Quick tab switching with shoulder buttons L and R (works from anywhere)
            if (kDown & HidNpadButton_L) {
                active_tab = (active_tab > 0) ? active_tab - 1 : 3;
                in_nav_rail = false;
                focused_index = (active_tab == 0) ? 1 : 0;
            }
            if (kDown & HidNpadButton_R) {
                active_tab = (active_tab < 3) ? active_tab + 1 : 0;
                in_nav_rail = false;
                focused_index = (active_tab == 0) ? 1 : 0;
            }

            // ========================================================
            // 1. NAVIGATION IN LEFT RAIL
            // ========================================================
            if (in_nav_rail) {
                // Up / Down moves between tabs with RGB rounded shimmer box
                if (kDown & (HidNpadButton_Up | HidNpadButton_StickLUp)) {
                    active_tab = (active_tab > 0) ? active_tab - 1 : 3;
                }
                if (kDown & (HidNpadButton_Down | HidNpadButton_StickLDown)) {
                    active_tab = (active_tab < 3) ? active_tab + 1 : 0;
                }

                // Right or A enters content area
                if (kDown & (HidNpadButton_Right | HidNpadButton_StickLRight | HidNpadButton_A)) {
                    in_nav_rail = false;
                    focused_index = (active_tab == 0) ? 1 : 0;
                }
            }
            // ========================================================
            // 2. NAVIGATION IN MAIN CONTENT AREA
            // ========================================================
            else {
                if (active_tab == 0) {
                    // Tab 0 (Chat): 0=Message bubble, 1=Input box, 2=Send button
                    if (kDown & (HidNpadButton_Up | HidNpadButton_StickLUp)) {
                        if (focused_index == 1 || focused_index == 2) {
                            focused_index = 0; // Go up to message bubble
                        } else if (focused_index == 0) {
                            // Scroll up by one paragraph/step
                            g_chat_scroll_y -= 45.0f;
                            g_auto_scroll = false;
                        }
                    }
                    if (kDown & (HidNpadButton_Down | HidNpadButton_StickLDown)) {
                        if (focused_index == 0) {
                            float total_h = UI_M3::GetTextHeight(g_chat_response.c_str(), 992);
                            float max_scr = std::max(0.0f, total_h - 332.0f);
                            if (g_chat_scroll_y >= max_scr - 10.0f) {
                                focused_index = 1; // Already at bottom: go down to input box
                            } else {
                                g_chat_scroll_y += 45.0f;
                                g_auto_scroll = false;
                            }
                        }
                    }
                    if (kDown & (HidNpadButton_Right | HidNpadButton_StickLRight)) {
                        if (focused_index == 1) {
                            focused_index = 2; // Move RIGHT to Send Button!
                        }
                    }
                    if (kDown & (HidNpadButton_Left | HidNpadButton_StickLLeft)) {
                        if (focused_index == 2) {
                            focused_index = 1; // Move LEFT back to Input Box!
                        } else if (focused_index == 1 || focused_index == 0) {
                            in_nav_rail = true; // Move LEFT into Navigation Rail!
                        }
                    }
                }
                else if (active_tab == 1) {
                    // Tab 1 (Models): Dynamic model list
                    int count = (int)g_model_mgr.getModelCount();
                    int max_idx = (count > 0) ? std::min(count - 1, 2) : 0;

                    if (kDown & (HidNpadButton_Up | HidNpadButton_StickLUp)) {
                        if (focused_index > 0) focused_index--;
                    }
                    if (kDown & (HidNpadButton_Down | HidNpadButton_StickLDown)) {
                        if (focused_index < max_idx) focused_index++;
                    }
                    if (kDown & (HidNpadButton_Left | HidNpadButton_StickLLeft)) {
                        in_nav_rail = true; // Move LEFT to Navigation Rail
                    }
                }
                else if (active_tab == 2) {
                    // Tab 2 (Overlay & Sysmodule: Coming Soon)
                    focused_index = 0;
                    if (kDown & (HidNpadButton_Left | HidNpadButton_StickLLeft | HidNpadButton_B)) {
                        in_nav_rail = true; // Move LEFT to Navigation Rail
                    }
                }
                else if (active_tab == 3) {
                    // Tab 3 (Settings): 0=Language, 1=Temperature, 2=Context, 3=CPU Boost
                    if (kDown & HidNpadButton_Y) {
                        g_settings.first_run = true;
                        g_settings.wizard_step = 0;
                        g_settings.wizard_focus = g_settings.language;
                    }
                    if (kDown & (HidNpadButton_Up | HidNpadButton_StickLUp)) {
                        if (focused_index > 0) focused_index--;
                    }
                    if (kDown & (HidNpadButton_Down | HidNpadButton_StickLDown)) {
                        if (focused_index < 3) focused_index++;
                    }
                    if (kDown & (HidNpadButton_Left | HidNpadButton_StickLLeft)) {
                        if (focused_index == 0) {
                            g_settings.language = (g_settings.language == 1) ? 0 : 1;
                            SaveConfig(g_settings, g_engine.GetLoadedModelPath());
                        } else if (focused_index == 1) {
                            g_settings.temperature = std::max(0.10f, g_settings.temperature - 0.05f);
                            SaveConfig(g_settings, g_engine.GetLoadedModelPath());
                        } else if (focused_index == 2) {
                            if (g_settings.context_size == 2048) g_settings.context_size = 1024;
                            else if (g_settings.context_size == 1024) g_settings.context_size = 512;
                            SaveConfig(g_settings, g_engine.GetLoadedModelPath());
                        } else if (focused_index == 3) {
                            g_settings.cpu_boost = !g_settings.cpu_boost;
                            SaveConfig(g_settings, g_engine.GetLoadedModelPath());
                        }
                    }
                    if (kDown & (HidNpadButton_Right | HidNpadButton_StickLRight)) {
                        if (focused_index == 0) {
                            g_settings.language = (g_settings.language == 0) ? 1 : 0;
                            SaveConfig(g_settings, g_engine.GetLoadedModelPath());
                        } else if (focused_index == 1) {
                            g_settings.temperature = std::min(1.50f, g_settings.temperature + 0.05f);
                            SaveConfig(g_settings, g_engine.GetLoadedModelPath());
                        } else if (focused_index == 2) {
                            if (g_settings.context_size == 512) g_settings.context_size = 1024;
                            else if (g_settings.context_size == 1024) g_settings.context_size = 2048;
                            SaveConfig(g_settings, g_engine.GetLoadedModelPath());
                        } else if (focused_index == 3) {
                            g_settings.cpu_boost = !g_settings.cpu_boost;
                            SaveConfig(g_settings, g_engine.GetLoadedModelPath());
                        }
                    }
                }

                // Return to Navigation Rail with B
                if (kDown & HidNpadButton_B) {
                    if (g_engine.IsGenerating()) {
                        g_engine.StopGeneration();
                        mutexLock(&g_chat_mutex);
                        g_chat_response += (g_settings.language == 1)
                            ? "\n\n[Generation stopped by user]"
                            : "\n\n[Генерация остановлена пользователем]";
                        mutexUnlock(&g_chat_mutex);
                    } else {
                        in_nav_rail = true;
                    }
                }

                // Action A inside content area
                if (kDown & HidNpadButton_A) {
                    if (active_tab == 0) {
                        if (focused_index == 1) {
                            // Open keyboard on input box
                            std::string text = SwkbdRu::show();
                            if (!text.empty()) {
                                strncpy(user_input, text.c_str(), sizeof(user_input) - 1);
                                user_input[sizeof(user_input) - 1] = '\0';
                            }
                        } else if (focused_index == 2) {
                            // Send prompt: append to dialogue history
                            if (!g_engine.IsGenerating() && strlen(user_input) > 0) {
                                mutexLock(&g_chat_mutex);
                                if (!g_chat_response.empty()) {
                                    g_chat_response += "\n\n----------------------------------------\n\n";
                                }
                                g_chat_response += (g_settings.language == 1) ? "You: \"" : "Вы: \"";
                                g_chat_response += user_input;
                                g_chat_response += "\"\n\nSwitchLM: ";
                                mutexUnlock(&g_chat_mutex);

                                g_auto_scroll = true; // Auto-scroll to track new response
                                g_engine.StartGenerationAsync(user_input, OnTokenReceived, nullptr, g_settings.temperature, g_settings.language);
                            }
                        } else if (focused_index == 0) {
                            // Pressing A on message bubble moves down to input box
                            focused_index = 1;
                        }
                    } else if (active_tab == 1) {
                        // Load selected model from discovered list
                        if (!g_engine.IsGenerating()) {
                            int count = (int)g_model_mgr.getModelCount();
                            if (focused_index >= 0 && focused_index < count) {
                                const DiscoveredModel* m = g_model_mgr.getModel(focused_index);
                                if (m) {
                                    bool ok = g_engine.LoadModelFromSD(m->path.c_str(), OnLoadProgress, nullptr);
                                    if (ok) {
                                        active_loaded_model_idx = focused_index;
                                        SaveConfig(g_settings, m->path);
                                        mutexLock(&g_chat_mutex);
                                        if (g_settings.language == 1) {
                                            g_chat_response = "Model loaded successfully:\n" + m->name + "\n\nSwitch to CHAT tab (press [L]) and ask anything!";
                                        } else {
                                            g_chat_response = "Модель успешно загружена:\n" + m->name + "\n\nПерейдите в ЧАТ (кнопка [L]) и задайте любой вопрос!";
                                        }
                                        mutexUnlock(&g_chat_mutex);
                                    } else {
                                        mutexLock(&g_chat_mutex);
                                        g_chat_response = (g_settings.language == 1)
                                            ? ("Error loading model:\n" + m->name)
                                            : ("Ошибка загрузки модели:\n" + m->name);
                                        mutexUnlock(&g_chat_mutex);
                                    }
                                }
                            }
                            g_load_progress = -1.0f;
                        }
                    } else if (active_tab == 2) {
                        // Overlay is coming soon - return to rail on A
                        in_nav_rail = true;
                    } else if (active_tab == 3) {
                        if (focused_index == 0) {
                            g_settings.language = (g_settings.language == 1) ? 0 : 1;
                            SaveConfig(g_settings, g_engine.GetLoadedModelPath());
                        } else if (focused_index == 3) {
                            g_settings.cpu_boost = !g_settings.cpu_boost;
                            SaveConfig(g_settings, g_engine.GetLoadedModelPath());
                        }
                    }
                }
            }

            // Right Stick: Analog smooth scrolling of chat history at ANY time
            HidAnalogStickState rstick = padGetStickPos(&pad, 1);
            if (abs(rstick.y) > 3500) {
                float delta = ((float)rstick.y / 32768.0f) * 14.0f;
                g_chat_scroll_y -= delta;
                g_auto_scroll = false;
            }

            // Clamp scrolling and auto-scroll update
            float total_text_h = UI_M3::GetTextHeight(g_chat_response.c_str(), 992);
            float visible_h = 332.0f;
            float max_scr = std::max(0.0f, total_text_h - visible_h);

            if (g_auto_scroll) {
                g_chat_scroll_y = max_scr;
            } else {
                g_chat_scroll_y = std::clamp(g_chat_scroll_y, 0.0f, max_scr);
                if (g_chat_scroll_y >= max_scr - 5.0f) {
                    g_auto_scroll = true; // Reached bottom, resume auto-tracking
                }
            }

            // Open keyboard with X at any time
            if (kDown & HidNpadButton_X) {
                std::string text = SwkbdRu::show();
                if (!text.empty()) {
                    strncpy(user_input, text.c_str(), sizeof(user_input) - 1);
                    user_input[sizeof(user_input) - 1] = '\0';
                    active_tab = 0;
                    in_nav_rail = false;
                    focused_index = 1; // Focus input box
                }
            }

            // Button Y: in Tab 0 clears dialogue history if on message bubble, or clears input text
            if (kDown & HidNpadButton_Y) {
                if (active_tab == 0 && focused_index == 0) {
                    mutexLock(&g_chat_mutex);
                    g_chat_response.clear();
                    g_chat_scroll_y = 0.0f;
                    g_auto_scroll = true;
                    mutexUnlock(&g_chat_mutex);
                } else {
                    user_input[0] = '\0';
                }
            }
        }

        // Thread-safe copy of chat response for rendering (up to 64 KB for long history)
        static char chat_render_buf[65536];
        mutexLock(&g_chat_mutex);
        size_t c_len = std::min(g_chat_response.size(), sizeof(chat_render_buf) - 1);
        memcpy(chat_render_buf, g_chat_response.c_str(), c_len);
        chat_render_buf[c_len] = '\0';
        mutexUnlock(&g_chat_mutex);

        // Render Frame
        frame_tick++;
        u32 stride = 0;
        u32* framebuf = (u32*)framebufferBegin(&fb, &stride);
        if (framebuf != nullptr && stride > 0) {
            ui_m3_render(framebuf, 1280, 720, stride / sizeof(u32),
                         active_tab, in_nav_rail, focused_index, frame_tick,
                         user_input, chat_render_buf, g_chat_scroll_y,
                         g_load_progress, g_model_mgr.getModels(),
                         active_loaded_model_idx, g_settings);
            framebufferEnd(&fb);
        }
    }

    g_engine.StopGeneration();
    g_engine.UnloadModel();
    ui_m3_shutdown();
    framebufferClose(&fb);

    if (romfs_mounted) {
        romfsExit();
    }
    return 0;
}