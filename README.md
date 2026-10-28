# SwitchLM Studio

[![Author](https://img.shields.io/badge/Author-vasin228-blueviolet.svg)](https://github.com/vasin228)
[![Platform](https://img.shields.io/badge/Platform-Nintendo%20Switch-E60012?logo=nintendoswitch&logoColor=white)](https://github.com/)
[![Architecture](https://img.shields.io/badge/Architecture-ARM64%20Cortex--A57-0091BD?logo=arm&logoColor=white)](https://github.com/)
[![SIMD](https://img.shields.io/badge/SIMD-ARM%20NEON%20Accelerated-green)](https://github.com/)
[![Inference Engine](https://img.shields.io/badge/Engine-llama.cpp%20Embedded-blue)](https://github.com/ggerganov/llama.cpp)
[![UI](https://img.shields.io/badge/UI-Material%20Design%203%20SDF-8A2BE2)](https://github.com/)
[![License](https://img.shields.io/badge/License-MIT-yellow.svg)](LICENSE)

**SwitchLM Studio** is an autonomous, fully offline Large Language Model (LLM) inference suite designed specifically for the Nintendo Switch (Horizon OS / Tegra X1). It embeds the official `llama.cpp` engine directly into a native Homebrew NRO, utilizing ARM64 NEON vector extensions across 3 CPU cores to run quantized neural networks locally without any internet connection.

---

## Features

- **Embedded Hardware LLM Inference**:
  - Full native port of `llama.cpp` for ARM64 Horizon OS.
  - Multi-threaded autoregressive decoding running on 3 Cortex-A57 CPU cores (Core 0, 1, 2).
  - ARMv8 NEON SIMD intrinsics (`vmlal_s8`, `vcvt_f32_f16`, `vaddlvq_s16`) for rapid quantization block dot products (`Q4_0`, `Q4_K_M`, `Q8_0`).
  - Strict compliance with Horizon OS memory architecture (`LLAMA_LOAD_MODE_NONE`, zero mmap dependencies, 4096-byte alignment).

- **Material 3 Fluent UI**:
  - Crisp, anti-aliased font rendering via `stb_truetype` with full English (ASCII) and Russian (Cyrillic `0x0400-0x04FF`, including `Ё/ё`) font atlases.
  - Subpixel Clamped Signed Distance Field (SDF) rounded cards, buttons, and navigation pills.
  - 60 FPS animated multi-color shimmering glow borders for active focus highlights.

- **Interactive Conversation Viewport**:
  - Strict viewport boundary clipping (`clip_y0`, `clip_y1`) preventing text overflow onto UI controls.
  - Smooth 60 FPS analog Right Stick (`R-Stick`) and D-Pad scrolling through long multi-turn dialogue history.
  - Dynamic scrollbar thumb indicator showing exact conversation position.
  - Automatic scroll tracking during real-time token generation.

- **Dynamic GGUF Model Manager**:
  - On-the-fly binary GGUF header parser scanning `sdmc:/switch/SwitchLM/models/`.
  - Validates `GGUF` magic, format version, and tensor counts (>0) while discarding dummy stubs.
  - Displays file size in MB, detected quantization format, and memory compatibility status.
  - Easily add any compatible `.gguf` file onto your SD card; it appears automatically in the catalog.

- **First-Launch Setup Wizard & Persistent Preferences**:
  - Guided setup on first run: choose preferred interface language (Russian / English) and initial model.
  - Settings persisted to `sdmc:/switch/SwitchLM/config.ini` (language, sampling temperature, context length, CPU boost, active model).
  - Horizon OS native screen keyboard (`swkbd`) supporting both Russian (ЙЦУКЕН) and English (QWERTY) layouts.

---

## Memory Modes & Requirements

| Launch Mode | Usable RAM | Maximum Model Size | Recommended Models |
| :--- | :--- | :--- | :--- |
| **Title Override** (Hold `[R]` while launching any installed game) | **~3200 MB** | Up to **2.2 GB** | `Qwen2.5-0.5B-Instruct-Q4_K_M` (468 MB)<br>`Qwen2.5-1.5B-Instruct-Q4_K_M` (980 MB)<br>`SmolLM2-1.7B-Q4_K_M` (1.05 GB) |
| **Applet Mode** (Album icon) | **~380 MB** | Up to **135 MB** | Experimental micro-models |

> **Important**: Always launch SwitchLM Studio using **Title Override** (hold `[R]` on an installed game) to grant the application access to 3.2 GB of console RAM.

---

## Repository Structure

```text
SwitchLM-Studio/
├── common/             # Shared engine modules (memory detection, UTF-8 streaming, IPC)
│   ├── mem_tier.c      # Title Override vs Applet mode detection
│   ├── utf8_stream.c   # Ring buffer for safe multi-byte UTF-8 token streaming
│   ├── swlm_ipc.c      # Shared memory IPC bridge
│   └── wiki_rag.c      # Zero-RAM binary index search engine
├── external/           # Vendored dependencies
│   ├── llama.cpp/      # Embedded llama.cpp engine & ARM64 headers
│   └── libtesla/       # Tesla overlay framework
├── studio/             # TARGET 1: SwitchLM-Studio.nro (Main Application)
│   ├── romfs/fonts/    # NotoSans-Regular.ttf font asset
│   ├── source/         # main.cpp, ui_m3.cpp, llm_engine.cpp, model_manager.cpp, swkbd_ru.cpp
│   └── Makefile        # devkitPro libnx Makefile
├── sysmodule/          # TARGET 2: Background Sysmodule (0100000000004C4D)
├── overlay/            # TARGET 3: Tesla In-Game Overlay (.ovl)
├── tools/              # Helper utilities
│   ├── build_wiki_idx.py        # Compiles text articles into binary search index
│   ├── colab_train_game_135m.py # Micro-model training script
│   └── deploy.ps1               # Automated DBI FTP deployment script
├── Makefile            # Root multi-target build script
└── README.md
```

---

## Building from Source

### Prerequisites

1. **devkitPro** with `devkitA64` and `libnx`:
   - Linux / WSL (Ubuntu): Follow official devkitPro pacman setup:
     ```bash
     sudo dkp-pacman -S switch-dev switch-libtesla
     ```
   - Ensure environment variables are set:
     ```bash
     export DEVKITPRO=/opt/devkitpro
     export DEVKITA64=$DEVKITPRO/devkitA64
     ```

2. **Embedded llama.cpp Library**:
   The static libraries `libllama.a` and `libggml-cpu.a` must be compiled using the devkitA64 toolchain:
   ```bash
   cd external/llama.cpp
   mkdir build2 && cd build2
   cmake .. -DCMAKE_TOOLCHAIN_FILE=../cmake/arm64-horizon.cmake \
            -DGGML_BUILD_TESTS=OFF -DGGML_BUILD_EXAMPLES=OFF
   make llama ggml ggml-cpu ggml-base -j4
   ```

### Compiling SwitchLM-Studio.nro

From the project root:
```bash
# Build main studio app
make studio

# Or compile directly inside the studio folder:
cd studio
make
```

The output executable will be created at `studio/SwitchLM-Studio.nro`.

---

## SD Card Installation

1. Copy `SwitchLM-Studio.nro` to your SD card:
   ```text
   sdmc:/switch/SwitchLM-Studio/SwitchLM-Studio.nro
   ```

2. Download your preferred GGUF neural network model (e.g. [Qwen2.5-0.5B-Instruct-Q4_K_M.gguf](https://huggingface.co/Qwen/Qwen2.5-0.5B-Instruct-GGUF)) and place it on the SD card:
   ```text
   sdmc:/switch/SwitchLM/models/Qwen2.5-0.5B-Instruct-Q4_K_M.gguf
   ```

3. (Optional) Configure `.env` and run the automated deployment script over your local network:
   ```bash
   cp .env.example .env
   # Set your console IP in .env (SWITCH_IP=192.168.1.100)
   powershell -ExecutionPolicy Bypass -File tools/deploy.ps1
   ```

4. Insert SD card into Nintendo Switch and launch Homebrew Menu via **Title Override** (hold `[R]` while launching any game). Select **SwitchLM Studio**.

---

## Gamepad Controls

### Chat View (Tab 0)
| Button / Stick | Action |
| :--- | :--- |
| **D-Pad / Left Stick** | Navigate between Message history, Input text bar, and [A] Send button |
| **[A]** | Open on-screen keyboard (on input bar) or Send prompt (on Send button) |
| **[X]** | Quick-open on-screen keyboard from anywhere |
| **[Y]** | Clear message history (when focused on chat) or clear input text |
| **Right Stick (`R-Stick`)** | Smooth 60 FPS analog scrolling of dialogue history |
| **[B]** | Stop generation in progress, or return to Left Navigation Rail |
| **[L] / [R]** | Quick cycle between tabs (Chat, Models, Overlay, Settings) |
| **[+]** | Exit application |

### Left Navigation Rail
| Button | Action |
| :--- | :--- |
| **D-Pad Up / Down** | Switch active tab with animated glowing focus highlight |
| **D-Pad Right / [A]** | Enter content area of current tab |

---

## Sampling Parameters & Settings (Tab 3)

SwitchLM Studio allows fine-tuning model inference directly from the console:
- **Interface Language**: Toggle between Russian and English.
- **Sampling Temperature**: Range `0.10` to `1.50` (Default: `0.45` for precise, grounded answers).
- **Repetition Penalty**: Preset to `1.15` (prevents looping on compact models).
- **Context Window**: Toggle between `512`, `1024`, and `2048` tokens.
- **CPU Boost**: Overclock 3 Cortex-A57 cores to 1785 MHz during active token generation.

All options automatically save to `sdmc:/switch/SwitchLM/config.ini`.

---

## Roadmap

- [x] Full native `llama.cpp` ARM64 NEON integration.
- [x] Material 3 subpixel SDF UI and animated glowing shimmer borders.
- [x] Russian & English bilingual interface with native Horizon OS on-screen keyboard (`swkbd`).
- [x] Analog Right Stick smooth scrolling & strict viewport clipping.
- [x] Dynamic GGUF model manager with memory safety validation.
- [x] First-launch setup wizard & persistent `config.ini`.
- [ ] In-game Tesla overlay (`SwitchLM-Overlay.ovl`) summoning via `[L + D-Pad Down + R3]`.
- [ ] Background daemon sysmodule (`SwitchLM-Sysmodule.nsp`) with Zero-RAM Wiki RAG search.

---

## Credits & Acknowledgments

- **vasin228** — Project author, architecture, UI design and Nintendo Switch integration.
- **[llama.cpp](https://github.com/ggerganov/llama.cpp)** by Georgi Gerganov and the open-source community for the efficient LLM inference engine.
- **[devkitPro](https://devkitpro.org/)** & **libnx** by WinterMute, fincs, and the homebrew development team.
- **[stb_truetype](https://github.com/nothings/stb)** by Sean Barrett for lightweight font rasterization.
- **[Qwen Team / Alibaba Cloud](https://github.com/QwenLM/Qwen2.5)** for the exceptional Qwen2.5 models.
- **[libtesla](https://github.com/WerWolv/libtesla)** by WerWolv for the Nintendo Switch overlay framework.
- All contributors and beta testers of the SwitchLM project.

---

## License

This project is licensed under the **MIT License** — see the [LICENSE](LICENSE) file for details.
All external submodules remain under their respective upstream licenses.
