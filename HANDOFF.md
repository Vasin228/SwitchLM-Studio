# SwitchLM Studio — Handoff & Architecture Briefing

**Project:** SwitchLM Studio  
**Target Platform:** Nintendo Switch (Horizon OS, ARM64 Cortex-A57, Tegra X1, Atmosphere 1.7+, Title Override 3200 MB RAM)  
**Status:** **FIRST-LAUNCH WIZARD, PERSISTENT CONFIG, SDF BORDER & DYNAMIC GGUF INTEGRATED**.  
**Hardware Target:** Nintendo Switch via DBI FTP.

---

## 1. Current State
- `SwitchLM-Studio.nro` (4.97 MB) compiled with static `libllama.a` and `libggml-cpu.a` using Cortex-A57 NEON SIMD kernels.
- **First-Launch Setup Wizard**:
  - Automatically triggers if `sdmc:/switch/SwitchLM/config.ini` does not exist.
  - Step 1: Language Selection (Русский [Recommended] vs English [Standard]).
  - Step 2: Model Selection (Discovered `.gguf` models on SD card with RAM status and recommended badge).
  - Transitions directly into Chat with chosen model active and persists settings to `config.ini`.
- **Dynamic GGUF Model Catalog**:
  - `ModelManager` dynamically scans `/switch/SwitchLM/models/` and `/switch/SwitchLM/micro/`.
  - Parses binary GGUF header (magic, version, tensor count > 0), file size, and quantization type.
  - Users can drop ANY `.gguf` file onto SD card; it will automatically appear in catalog.
- **Fixed Notched Border ("Кривая обводка")**:
  - Fully unified clamped SDF (Signed Distance Field) formula in `draw_rounded_shimmer_border` and pixel-accurate `draw_rounded_rect`.
  - Zero seams or stepped notches across corner radii (`r=16` cards/buttons, `r=18` pills, `r=24` modals).
- **Fixed Question Marks (`??????????`)**:
  - Divider changed to ASCII dashes `----------------------------------------`.
  - Codepoint mapping in `GetTextWidth`, `GetTextHeight`, `DrawTextUTF8` maps `0x2500` (─) and typographics dashes/bullets to `-`.
- **Improved Sampling Quality**:
  - Added repetition penalty `llama_sampler_init_penalties(..., 64, 1.15f, 0.0f, 0.0f)`.
  - Temperature set to `0.45f` and Top-P to `0.85f` for grounded, hallucination-free output from Qwen 0.5B.
- **Persistent Settings (Tab 3 & config.ini)**:
  - Settings persisted: `language` (RU/EN), `temperature`, `context_size`, `cpu_boost`, `model_path`.

---

## 2. Directory Layout & Key Files

```text
SwitchLM-Studio/
├── Makefile                     # Root build script
├── common/                      # Shared across targets (mem_tier, utf8_stream, wiki_rag)
├── studio/                      # TARGET 1: SwitchLM-Studio.nro
│   ├── romfs/fonts/             # NotoSans-Regular.ttf
│   └── source/                  # main.cpp, ui_m3.hpp/.cpp, llm_engine.hpp/.cpp, model_manager.hpp/.cpp, swkbd_ru.hpp/.cpp
├── sysmodule/                   # TARGET 2: SwitchLM-Sysmodule.nsp (0100000000004C4D)
├── overlay/                     # TARGET 3: SwitchLM-Overlay.ovl (Tesla in-game overlay)
├── tools/deploy.ps1             # Automated DBI FTP deployment script (with size skip)
└── sdout/                       # Deployment bundle matching Switch SD card root
```

---

## 3. Verified Architecture Facts
1. **Horizon OS MMAP Prohibition**: `llama_model_load_from_file` MUST use `load_mode = LLAMA_LOAD_MODE_NONE`.
2. **Missing Libc Symbols Wrapped**: `posix_memalign` (via `memalign`) and `sysconf` (`_SC_PAGESIZE=4096`, `_SC_PHYS_PAGES=819200`) in `llm_engine.cpp`.
3. **Viewport Bounds**: Message area spans `cw - 48` wide, `360px` high. Text clipped with `clip_y0 = msg_y + 14`, `clip_y1 = msg_y + msg_h - 14`.
4. **Scrolling Controls**: Right Analog Stick (smooth float delta), D-Pad Up/Down (45px step), auto-scroll tracking streaming generation.
5. **Config File**: `sdmc:/switch/SwitchLM/config.ini` stores `first_run`, `language`, `temperature`, `context_size`, `cpu_boost`, `model_path`.

---

## 4. Verification Instructions for Human
1. Launch `SwitchLM Studio` via **Title Override** (hold [R] on any game).
2. On first launch, verify the **Setup Wizard**:
   - Step 1: Select language (Русский / English) with [LEFT/RIGHT] and confirm with [A].
   - Step 2: Select `Qwen2.5-0.5B-Instruct-Q4_K_M.gguf` and press [A].
3. Verify that the app transitions straight to Chat with the model active.
4. Check the focus border on `[A] Отправить`: all 4 corners are smooth with no jagged notches.
5. Send a prompt (e.g. "Привет, как тебя зовут?"):
   - Divider line between messages is clean dashes (no `???` marks).
   - Answer streams in clean Russian with zero repetition loops.
   - Right stick smoothly scrolls past responses.