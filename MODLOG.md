# SwitchLM Studio Development Log

## 2026-10-28 (First-Launch Wizard, Persistent Config, SDF Border & Glyph Fix)
- **First-Launch Setup Wizard & Persistent Config (`config.ini`)**:
  - Implemented interactive setup wizard modal dialog displayed on first run when `sdmc:/switch/SwitchLM/config.ini` is absent.
  - Step 1: Language selection (Русский [Recommended] vs English [Standard]).
  - Step 2: Model selection from discovered models on SD card with RAM status indicator and recommended badge.
  - Saves all preferences (`language`, `temperature`, `context_size`, `cpu_boost`, `model_path`, `first_run=0`) to `sdmc:/switch/SwitchLM/config.ini`.
  - Settings tab (Tab 3) updated with language toggle and real-time persistent saving on any parameter modification.
- **Dynamic GGUF Model Parser (Answering User Query)**:
  - Verified that `ModelManager` is 100% dynamic: it parses the binary GGUF header (`GGUF` magic, format version, `tensor_count > 0`), reads file size, and detects quantization scheme. Any user can place any `.gguf` into `sdmc:/switch/SwitchLM/models/` and it will automatically be cataloged.
- **Unified Signed Distance Field (SDF) Shimmer Border (Fixing Photo 2)**:
  - Rewrote `draw_rounded_shimmer_border` in `ui_m3.cpp` with a unified clamped distance function: `qx = clamp(px, ix0, ix1)`, `qy = clamp(py, iy0, iy1)`, `dist = sqrt(dx^2 + dy^2) - r`.
  - Eliminated split piecewise code paths, removing all notched/jagged corner seams on focused buttons (`[A] Отправить`, cards, nav pills).
  - Updated `draw_rounded_rect` to strictly span `[x, x + w - 1]` with exact corner center positions.
- **Question Mark Character Fix (Fixing Photo 1)**:
  - Identified cause of `????????????????????????????????????????`: separator in `main.cpp` used Unicode codepoint `0x2500` (`─`), unmapped in font atlas.
  - Replaced divider in `main.cpp` with ASCII dashes `----------------------------------------`.
  - Added codepoint mapping in `GetTextWidth`, `GetTextHeight`, and `DrawTextUTF8` in `ui_m3.cpp` to map `0x2500` (─) and typographical dashes (`—`, `–`, `―`) to `-`.
- **Sampling Quality Optimization for Qwen 0.5B**:
  - Configured `llama_sampler_init_penalties(..., 64, 1.15f, 0.0f, 0.0f)` to prevent repetitive looping.
  - Lowered default temperature to `0.45f` and Top-P to `0.85f`.
  - Added bilingual ChatML system prompts adapted to chosen interface language.
- **Deployment**:
  - Built `SwitchLM-Studio.nro` (4.97 MB) with devkitA64 in WSL.
  - Uploaded to Switch via DBI FTP (status 226).

## 2026-10-28 (Scrollable Chat History, Viewport Clipping & Zero-Tensor Model Filter)
- **Chat Viewport Clipping (`clip_y0` / `clip_y1`)**: Added strict vertical bounding in `UI_M3::DrawTextUTF8`. Text pixels outside `[clip_y0, clip_y1)` are clipped. Eliminates text overflow over the input row and navigation hints.
- **Scroll Engine & History Accumulation**:
  - Added dialogue history persistence: past queries and responses are appended (`Вы: "..." \n\n SwitchLM: ...`) separated by dividers rather than overwritten.
  - Implemented analog smooth 60fps scrolling using Right Stick (`padGetStickPos(&pad, 1)`).
  - Added D-Pad Up/Down scrolling when focused on message bubble (`focused_index == 0`).
  - Added dynamic scrollbar indicator tracking `chat_scroll_y / max_scroll` with active focus glow.
  - Added auto-scroll tracking new tokens during generation, pausing when the user scrolls up to review history.
  - Bound button `[Y]` to clear dialogue history when on message bubble, or clear input field.
- **Zero-Tensor Model Filter**:
  - In `model_manager.cpp`, added GGUF header validation checking `tensor_count > 0`.
  - Deleted dummy 0-tensor file `SwitchLM-GameAssist-135M-Q3_K_M.gguf` from Switch SD card via FTP. Only valid neural models appear in the catalog.
- **Deployment**: Rebuilt `SwitchLM-Studio.nro` (4.8 MB) and deployed via DBI FTP (status 226).

## 2026-10-28 (Real llama.cpp On-Device Neural Inference & Dynamic Model Catalog)
- **Root Cause Identified**: Previous builds contained mock keyword templates for game hints in `llm_engine.cpp` while `llama.h` was a stub header. Real model weights were not being decoded, causing identical canned responses regardless of the prompt.
- **Full llama.cpp Integration**:
  - Connected official `external/llama.cpp/include/llama.h` directly to `studio/source/llm_engine.cpp`.
  - Resolved `posix_memalign` and `sysconf` missing libc symbols in devkitA64 newlib with native implementations (`memalign`, `_SC_PAGESIZE`, `_SC_PHYS_PAGES`).
  - Solved CMake object conflict where `ggml-cpu.c` shadowed `ggml-cpu.cpp` by building `ggml-cpu-cpp.o` and archiving it into `libggml-cpu.a` with `ggml_backend_cpu_reg()`.
  - Re-linked `SwitchLM-Studio.nro` (4.8 MB) with `-Wl,--start-group -lllama -lggml -lggml-cpu -lggml-base -Wl,--end-group`.
- **Hardware Autoregressive Decoding Loop**:
  - Implemented `llama_model_load_from_file` with `LLAMA_LOAD_MODE_NONE` (strictly no mmap on Horizon OS).
  - Configured 1024 context tokens, 128 batch size, 3 Cortex-A57 worker threads (Cores 0, 1, 2 via mask `0x07`), and 1 MB worker thread stack.
  - Formatted prompts with Qwen ChatML template (`<|im_start|>system...<|im_start|>user...<|im_start|>assistant`).
  - Added sampling chain (Top-K 40, Top-P 0.9, Temp 0.7) and token-by-token streaming to `Utf8Stream` callback.
- **Dynamic Models Tab & Preloading**:
  - Refactored `ModelManager` and `ui_m3.cpp`: removed hardcoded cards and duplicates. Discovered models are displayed dynamically with active RAM indicators.
  - `Qwen2.5-0.5B-Instruct-Q4_K_M.gguf` (468.6 MB) sorted first and auto-preloaded on app launch in Title Override mode.
- **Deployment**: Successfully transferred `SwitchLM-Studio.nro` (4.8 MB) and `Qwen2.5-0.5B-Instruct-Q4_K_M.gguf` to Switch via DBI FTP (status 226).

## 2026-10-28 (Russian Keyboard, Permanent Qwen Catalog & Conversational AI)
- **Russian Swkbd (`SwkbdType_Normal`)**: Replaced `SwkbdType_QWERTY` in `swkbd_ru.cpp` with `SwkbdType_Normal`. This restores native Russian (ЙЦУКЕН) layout on Horizon OS with the standard globe language-switching button.
- **Quote Character Fix**: Mapped Unicode typographic angle quotes `«` (0x00AB) and `»` (0x00BB) in `ui_m3.cpp` to standard double quotes `"` so prompts never render `?` marks.
- **Uniform RGB Wave**: Replaced polar angle with physical perimeter arc length tracking (`s_dist / P`) across straight segments and corner arcs for constant wave velocity.

## 2026-10-28 (Rounded Shimmer Border, Centered Layout & Title Override Fix)
- **Rounded Shimmer Border (`draw_rounded_shimmer_border`)**: Subpixel Euclidean distance based rounded border matching exact corner radii (`r=18` for nav pills, `r=16` for cards/inputs).
- **Navigation Rail Centering**: Widened left rail to 152px and pills to 130px with `UI_M3::GetTextWidth()` mathematical centering.
- **Title Override Detection**: Fixed `mem_tier.c` using `appletGetAppletType() == AppletType_Application` to detect Title Override with 100% accuracy (~3200 MB RAM).