# llama.cpp Integration Guide

EDGESCRIBE uses **llama.cpp** for LLM and Vision inference with GGUF models,
alongside **onnxruntime-genai** for ASR and **onnxruntime** for TTS.

---

## Table of Contents

1. [Architecture Overview](#architecture-overview)
2. [Build from Source — Step by Step](#build-from-source--step-by-step)
3. [CI/CD Pipeline](#cicd-pipeline)
4. [CMake Build System](#cmake-build-system)
5. [Model Management](#model-management)
6. [Engine APIs](#engine-apis)
7. [GPU Acceleration](#gpu-acceleration)
8. [Packaging and Distribution](#packaging-and-distribution)
9. [Migration Notes](#migration-notes)
10. [Troubleshooting](#troubleshooting)

---

## Architecture Overview

```
edgescribe.exe
│
├── llama.cpp            → LLM (chat, SOAP, summarize) + Vision (OCR, image analysis)
│   Model format:          GGUF (single file, self-contained)
│   Model:                 Qwen3-VL-2B Q4_K_M (~990 MB)
│   GPU support:           CUDA, Metal, Vulkan
│
├── onnxruntime-genai    → ASR (speech-to-text)
│   Model format:          ONNX (multiple files)
│   Model:                 Parakeet TDT 0.6B (~670 MB)
│   GPU support:           CUDA, DirectML, CoreML
│
└── onnxruntime          → TTS (text-to-speech)
    Model format:          ONNX (multiple files)
    Model:                 Kokoro/Piper (~60–300 MB)
    GPU support:           Same as above (shared runtime)
```

### Why This Hybrid?

| Runtime | Used For | Why Not Use It For Everything? |
|---------|----------|-------------------------------|
| **llama.cpp** | LLM + Vision | Cannot do ASR or TTS — no audio model support |
| **onnxruntime-genai** | ASR | Only runtime with `OgaStreamingProcessor` for Nemotron |
| **onnxruntime** | TTS | Kokoro/Piper are ONNX-native; ORT already loaded for ASR |

### Package Size Breakdown

| Component | Size |
|-----------|------|
| Runtime libs (llama + ORT + ORT-GenAI) | ~19 MB |
| ASR model (Nemotron, ONNX) | ~670 MB |
| LLM+Vision model (Qwen3-VL, GGUF) | ~990 MB |
| TTS model (Kokoro, ONNX) | ~300 MB |
| **Total** | **~1.96 GB** |

---

## Build from Source — Step by Step

### Prerequisites

| Tool | Version | Purpose |
|------|---------|---------|
| CMake | 3.18+ | Build system |
| C++ compiler | C++20 (MSVC 2022 / Clang 14+ / GCC 12+) | Compilation |
| Git | Any | Clone repos |
| curl | Any | Download headers |

### Step 1: Clone EDGESCRIBE

```bash
git clone https://github.com/EDGESCRIBE/EDGESCRIBE.git
cd EDGESCRIBE
```

### Step 2: Download miniaudio header

```bash
# Single-header audio library (not committed to repo)
curl -sL -o include/miniaudio.h \
  https://raw.githubusercontent.com/mackron/miniaudio/master/miniaudio.h
```

### Step 3: Build llama.cpp

```bash
git clone https://github.com/ggerganov/llama.cpp.git
cd llama.cpp
```

Choose your build variant:

**Vulkan — RECOMMENDED (GPU acceleration with CPU fallback):**

This is the default for distributed builds. Works on NVIDIA, AMD, and Intel GPUs.
If no GPU is available at runtime, automatically falls back to CPU — no crash, no
missing DLLs.

```bash
# Linux — install Vulkan dev headers first
sudo apt-get install -y libvulkan-dev

cmake -B build -DCMAKE_BUILD_TYPE=Release \
      -DGGML_VULKAN=ON \
      -DLLAMA_BUILD_TESTS=OFF \
      -DLLAMA_BUILD_EXAMPLES=OFF \
      -DLLAMA_BUILD_SERVER=OFF

cmake --build build --config Release
cmake --install build --prefix ./install
```

**Windows (PowerShell) — Vulkan:**
```powershell
# Install Vulkan SDK from https://vulkan.lunarg.com/sdk/home
# Or: winget install LunarG.VulkanSDK

cmake -B build -DCMAKE_BUILD_TYPE=Release `
      -DGGML_VULKAN=ON `
      -DLLAMA_BUILD_TESTS=OFF `
      -DLLAMA_BUILD_EXAMPLES=OFF `
      -DLLAMA_BUILD_SERVER=OFF

cmake --build build --config Release
cmake --install build --prefix ./install --config Release
```

**macOS Apple Silicon (Metal):**
```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release \
      -DGGML_METAL=ON \
      -DLLAMA_BUILD_TESTS=OFF \
      -DLLAMA_BUILD_EXAMPLES=OFF \
      -DLLAMA_BUILD_SERVER=OFF

cmake --build build --config Release
cmake --install build --prefix ./install
```

**NVIDIA-only (CUDA) — for maximum NVIDIA performance:**

Only use this if you ONLY target NVIDIA GPUs. The binary will CRASH on machines
without CUDA toolkit installed.

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release \
      -DGGML_CUDA=ON \
      -DLLAMA_BUILD_TESTS=OFF \
      -DLLAMA_BUILD_EXAMPLES=OFF \
      -DLLAMA_BUILD_SERVER=OFF

cmake --build build --config Release
cmake --install build --prefix ./install
```

**CPU-only (no GPU acceleration):**
```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release \
      -DLLAMA_BUILD_TESTS=OFF \
      -DLLAMA_BUILD_EXAMPLES=OFF \
      -DLLAMA_BUILD_SERVER=OFF

cmake --build build --config Release
cmake --install build --prefix ./install
```

After building, the install directory should contain:
```
llama.cpp/install/
├── include/
│   ├── llama.h          # Core LLM API
│   ├── mtmd.h           # Multimodal (vision) API
│   ├── mtmd-helper.h    # Vision helper functions
│   └── ggml.h           # Low-level tensor library
├── bin/                  # Windows: DLLs here
│   ├── llama.dll
│   ├── ggml.dll / ggml-base.dll / ggml-cpu.dll
│   ├── ggml-vulkan.dll  # Only if built with Vulkan
│   └── mtmd.dll
└── lib/
    ├── llama.lib         # Windows import library
    ├── mtmd.lib
    └── ...
```

```bash
cd ..  # back to EDGESCRIBE root
```

### Step 4: Download onnxruntime-genai

Download pre-built binaries from [onnxruntime-genai releases](https://github.com/microsoft/onnxruntime-genai/releases):

**Linux:**
```bash
GENAI_VERSION="0.12.0"
curl -sL "https://github.com/microsoft/onnxruntime-genai/releases/download/v${GENAI_VERSION}/onnxruntime-genai-${GENAI_VERSION}-linux-x64.tar.gz" \
  | tar xz -C genai_extracted
```

**macOS (Apple Silicon):**
```bash
GENAI_VERSION="0.12.0"
curl -sL "https://github.com/microsoft/onnxruntime-genai/releases/download/v${GENAI_VERSION}/onnxruntime-genai-${GENAI_VERSION}-osx-arm64.tar.gz" \
  | tar xz -C genai_extracted
```

**Windows (PowerShell):**
```powershell
$version = "0.12.0"
$url = "https://github.com/microsoft/onnxruntime-genai/releases/download/v$version/onnxruntime-genai-$version-win-x64.zip"
Invoke-WebRequest -Uri $url -OutFile genai.zip
Expand-Archive genai.zip -DestinationPath genai_extracted
```

The extracted directory should contain:
```
genai_extracted/
├── include/
│   ├── ort_genai.h
│   ├── ort_genai_c.h
│   └── onnxruntime_cxx_api.h
└── lib/
    ├── onnxruntime-genai.dll / .so / .dylib
    ├── onnxruntime.dll / .so / .dylib
    └── onnxruntime_providers_shared.dll / .so
```

### Step 5: Configure and Build EDGESCRIBE

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release \
      -DORT_GENAI_PATH=./genai_extracted \
      -DLLAMA_CPP_PATH=./llama.cpp/install

cmake --build build --config Release
```

### Step 6: Verify

```bash
# Check binary runs
./build/edgescribe --version

# Download a model and test
./build/edgescribe pull qwen3-vl
./build/edgescribe chat "Hello, what can you do?"
```

---

## CI/CD Pipeline

The GitHub Actions workflow (`.github/workflows/build.yml`) builds EDGESCRIBE for all
platforms. It downloads both llama.cpp and onnxruntime-genai as pre-built packages.

### Supported Platforms

| Platform | Runner | llama.cpp build | Notes |
|----------|--------|-----------------|-------|
| Windows x64 | `windows-latest` | CPU (MSVC) | + Inno Setup installer |
| macOS ARM64 | `macos-14` | Metal (Apple Silicon) | M1/M2/M3 GPU acceleration |
| macOS x64 | `macos-13` | CPU (Intel) | |
| Linux x64 | `ubuntu-22.04` | CPU | |

### Workflow Summary

```
Trigger: push to main, tags v*, pull requests
    │
    ├─ Download miniaudio.h
    ├─ Build llama.cpp from source (per-platform config)
    ├─ Download onnxruntime-genai pre-built release
    ├─ Configure CMake with both LLAMA_CPP_PATH and ORT_GENAI_PATH
    ├─ Build edgescribe
    ├─ Package (ZIP on Windows, tar.gz on macOS/Linux)
    ├─ Build Windows installer (Inno Setup)
    └─ Upload artifacts
         │
         └─ On tag push → Create GitHub Release with all artifacts
```

### Artifacts Produced

| Artifact | GPU Backend | Contents |
|----------|:---:|----------|
| `openscribe-win-x64.zip` | Vulkan | edgescribe.exe + DLLs (GPU + CPU fallback) |
| `openscribe-osx-arm64.tar.gz` | Metal | edgescribe + dylibs (Apple GPU) |
| `openscribe-osx-x64.tar.gz` | Vulkan | edgescribe + dylibs (GPU + CPU fallback) |
| `openscribe-linux-x64.tar.gz` | Vulkan | edgescribe + shared libs (GPU + CPU fallback) |
| `OpenScribeSetup.exe` | Vulkan | Windows installer (adds to PATH) |

---

## CMake Build System

### Variables

| Variable | Required | Description |
|----------|----------|-------------|
| `ORT_GENAI_PATH` | **Yes** | Path to onnxruntime-genai (include/ + lib/) |
| `LLAMA_CPP_PATH` | **Yes** | Path to llama.cpp install (include/ + lib/) |
| `CMAKE_BUILD_TYPE` | Recommended | `Release` for production builds |

### Compile Definitions

When `LLAMA_CPP_PATH` is provided:
- `EDGESCRIBE_USE_LLAMA_CPP` — enables llama.cpp code paths

Always defined:
- `EDGESCRIBE_VERSION` — project version string
- `EDGESCRIBE_PLATFORM` — platform RID (win-x64, osx-arm64, etc.)

### Header Search Order

**llama.cpp headers:**
1. `${LLAMA_CPP_PATH}/include/llama.h`
2. `${LLAMA_CPP_PATH}/llama.h`

**ORT-GenAI headers:**
1. `${ORT_GENAI_PATH}/include/ort_genai.h`
2. `${ORT_GENAI_PATH}/ort_genai.h`
3. `${ORT_GENAI_PATH}/src/ort_genai.h`

### Library Search Order

**llama library:**
1. `${LLAMA_CPP_PATH}/lib/`
2. `${LLAMA_CPP_PATH}/build/src/Release/`
3. `${LLAMA_CPP_PATH}/build/Release/`
4. `${LLAMA_CPP_PATH}/build/bin/Release/`

### Post-Build Steps

All shared libraries from both `LLAMA_CPP_PATH/lib/` and `ORT_GENAI_PATH/lib/` are
automatically copied to the build output directory, ensuring the binary is self-contained.

---

## Model Management

### Downloading Models

```bash
edgescribe pull nemotron    # ASR  — Parakeet TDT 0.6B (670 MB, ONNX)
edgescribe pull qwen3-vl    # LLM+Vision — Qwen3-VL-2B Q4_K_M (990 MB, GGUF)
edgescribe pull kokoro      # TTS  — Kokoro (300 MB, ONNX)
```

### Model Cache

| Platform | Default Path | Override |
|----------|-------------|----------|
| Windows | `%LOCALAPPDATA%\EDGESCRIBE\models\` | `EDGESCRIBE_MODEL_DIR` |
| macOS | `~/.EDGESCRIBE/models/` | `EDGESCRIBE_MODEL_DIR` |
| Linux | `~/.EDGESCRIBE/models/` | `EDGESCRIBE_MODEL_DIR` |

### Model Details

| Model | Format | Files | Size | HuggingFace Repo |
|-------|--------|-------|------|-----------------|
| nemotron | ONNX | encoder.onnx, decoder.onnx, joint.onnx + configs | 670 MB | EDGESCRIBE/nemotron-onnx-cpu |
| qwen3-vl | **GGUF** | Qwen3VL-2B-Instruct-Q4_K_M.gguf + mmproj-F16.gguf | 1.8 GB | Qwen/Qwen3-VL-2B-Instruct-GGUF |
| kokoro | ONNX | onnx/model.onnx (FP32) + voice .bin files | 310 MB | onnx-community/Kokoro-82M-v1.0-ONNX |

### Qwen3-VL Model Files (2 GGUF files)

| File | Purpose | Size |
|------|---------|:---:|
| `Qwen3VL-2B-Instruct-Q4_K_M.gguf` | LLM brain (text reasoning, tokenizer embedded) | 1.0 GB |
| `mmproj-Qwen3VL-2B-Instruct-F16.gguf` | Vision projector (translates image features → LLM embeddings) | 781 MB |

### Kokoro TTS Model Files

```
kokoro/
├── onnx/
│   └── model.onnx          (310 MB, FP32)
├── config.json
└── voices/
    ├── af_heart.bin          (female voice)
    ├── af_sky.bin            (female voice)
    ├── am_michael.bin        (male voice)
    └── am_adam.bin           (male voice)
```

All models are **public and ungated** — no HuggingFace token required for download.

### GGUF vs ONNX — Why GGUF for LLM/Vision?

| Property | GGUF (Qwen3-VL) | ONNX (previous) |
|----------|:---:|:---:|
| File count | 1 | 7 |
| Model size | 990 MB | 1500 MB |
| Tokenizer | Embedded in file | Separate JSON files |
| Config | Embedded metadata | Separate JSON files |
| Quantization | Q4_K_M (mixed-precision per block) | INT4 (uniform per tensor) |
| Quality-at-size | Better (mixed precision) | Good |

---

## Engine APIs

### LLM Engine (llama.cpp)

```cpp
#include "llm/llm_engine.h"

// Load model — auto-detects GPU, falls back to CPU
EDGESCRIBE::llm::LlmEngine engine("/path/to/model.gguf");

// Force CPU only
EDGESCRIBE::llm::LlmEngine engine("/path/to/model.gguf", "cpu");

// Single-turn chat
std::string result = engine.Chat("You are helpful.", "What is aspirin?");

// Streaming output
engine.Chat("system prompt", "user message", 2048,
    [](const std::string& token) { std::cout << token << std::flush; });

// Multi-turn chat (KV cache reused automatically — only new tokens processed)
std::vector<EDGESCRIBE::llm::ChatMessage> messages = {
    {"system", "You are a medical assistant."},
    {"user", "What is hypertension?"},
    {"assistant", "High blood pressure..."},
    {"user", "How is it treated?"},
};
engine.Chat(messages, 2048, callback);

// Task-specific
engine.GenerateSOAPNotes(transcript, callback);
engine.Summarize(text, callback);
engine.FixMedicalTerms(transcript, callback);

// Invalidate KV cache (call when starting a new conversation)
engine.InvalidateCache();
```

### KV Cache Optimization

Multi-turn chat automatically reuses the KV cache from previous turns:

```
Turn 1:  [system + user1]                → 200 tokens processed, TTFT ~500ms
Turn 2:  [system + user1 + asst1 + user2] → only 200 NEW tokens, TTFT ~50ms
Turn 10: [full history]                    → only 200 NEW tokens, TTFT ~50ms
```

Performance output shows cache stats:
```
prompt: 2000 tokens | cached: 1800 | new: 200 | 400.0 tok/s | TTFT: 50 ms
output: 150 tokens | 15.8 tok/s | 9500 ms
```

### Vision Engine (llama.cpp + mtmd)

```cpp
#include "vision/vision_engine.h"

EDGESCRIBE::vision::VisionEngine engine("/path/to/model.gguf", "cpu");

// Analyze image
std::string desc = engine.Analyze("photo.jpg", "Describe this image.");

// Multiple images
std::vector<std::string> images = {"img1.jpg", "img2.jpg"};
engine.Analyze(images, "Compare these images.", 2048, callback);

// OCR
std::string text = engine.OCR("document.png");

// Medical
engine.DescribeMedicalImage("xray.jpg", callback);
engine.GenerateSOAPNotes(transcript, "photo.jpg", callback);
```

### Vision Model Files

Qwen3-VL requires two GGUF files:
1. **Main model** — `qwen3-vl-2b-instruct-q4_k_m.gguf` (LLM weights)
2. **Vision projector** — `*-mmproj-f16.gguf` (CLIP encoder)

The vision engine auto-discovers the mmproj file by looking for:
- `{model_basename}-mmproj-f16.gguf` (same directory)
- `mmproj-model-f16.gguf` (fallback)

If not found, vision features are disabled; text-only generation still works.

### ASR Engine (onnxruntime-genai) — Unchanged

```cpp
#include "asr/transcriber.h"

// Uses OgaStreamingProcessor — requires onnxruntime-genai
Transcriber transcriber("/path/to/nemotron/", "cpu");
```

### TTS Engine (onnxruntime) — Unchanged

```cpp
#include "tts/tts_engine.h"

// Uses Ort::Session — requires onnxruntime
TtsEngine tts("/path/to/kokoro/");
auto audio = tts.Synthesize("Hello, world!");
```

---

## GPU Acceleration

### GPU Backend Strategy

EDGESCRIBE ships with **Vulkan** as the universal GPU backend. This provides:

```
Vulkan build behavior at runtime:
  ├─ NVIDIA GPU present   → ✅ Uses Vulkan GPU acceleration
  ├─ AMD GPU present      → ✅ Uses Vulkan GPU acceleration
  ├─ Intel GPU present    → ✅ Uses Vulkan GPU acceleration
  ├─ No GPU / old GPU     → ✅ Falls back to CPU automatically
  └─ No Vulkan runtime    → ✅ Falls back to CPU automatically
```

**One binary works for all users.** No separate CUDA/CPU builds needed.

### Why Vulkan over CUDA?

| | Vulkan | CUDA |
|---|:---:|:---:|
| NVIDIA GPUs | ✅ | ✅ |
| AMD GPUs | ✅ | ❌ |
| Intel GPUs | ✅ | ❌ |
| CPU fallback if no GPU | ✅ Graceful | ❌ Crashes |
| Missing runtime DLL | ✅ Still runs | ❌ Won't start |
| Performance on NVIDIA | ~90% of CUDA | 100% (best) |

Vulkan is ~10% slower than CUDA on NVIDIA, but works everywhere. For users
who need maximum NVIDIA performance, they can build from source with `DGGML_CUDA=ON`.

### Device Selection

GPU is **enabled by default** — no flag needed. llama.cpp auto-detects available
GPU backends (Vulkan, CUDA, Metal) and offloads layers automatically. If no GPU
is available, it silently falls back to CPU.

| Device | LLM/Vision (llama.cpp) | ASR (onnxruntime-genai) |
|--------|:---:|:---:|
| (default, no flag) | **Auto GPU** → CPU fallback | CPU execution provider |
| `cpu` | Force CPU only | CPU execution provider |
| `cuda` | Force CUDA | CUDA execution provider |
| `metal` | Force Metal | N/A |
| `vulkan` | Force Vulkan | N/A |

```bash
# Auto GPU (recommended — just works)
edgescribe chat "Hello"
edgescribe serve --port 8080

# Force CPU only
edgescribe chat "Hello" --device cpu
```

**Layer splitting**: If the model doesn't fully fit in GPU VRAM, llama.cpp
automatically splits layers — some on GPU, some on CPU. No configuration needed.
For example, on a 4 GB GPU, ~70% of layers may go to GPU and the rest to CPU.

### Sampling Parameters

Default values used in LLM/Vision engines:

| Parameter | Value | Purpose |
|-----------|-------|---------|
| Temperature | 0.6 | Controls randomness |
| Top-P | 0.9 | Nucleus sampling threshold |
| Min-P | 0.05 | Minimum probability filter |
| Context size | 4096 | Maximum token window |
| Batch size | 512 | Prompt processing batch |

---

## Packaging and Distribution

### Runtime Files

A complete EDGESCRIBE distribution contains:

```
edgescribe-win-x64/
├── edgescribe.exe              (~1 MB)     Application binary
├── llama.dll                   (~2 MB)     LLM + Vision inference
├── ggml.dll                    (~0.1 MB)   Backend loader
├── ggml-base.dll               (~0.5 MB)   Core tensor ops
├── ggml-cpu.dll                (~0.9 MB)   CPU backend (always used as fallback)
├── ggml-vulkan.dll            (~54 MB)     Vulkan GPU backend (auto-fallback to CPU)
├── mtmd.dll                    (~0.7 MB)   Multimodal vision (mmproj processing)
├── onnxruntime.dll            (~13.5 MB)   TTS runtime
├── onnxruntime-genai.dll       (~2.3 MB)   ASR runtime
├── onnxruntime_providers_shared.dll
├── vc_redist_x64.exe          (~24 MB)     VC++ runtime (run if DLL errors)
├── www/                                    Web frontend UI
├── README.md
└── LICENSE
```

### Windows Installer

The Inno Setup installer (`installer/windows/edgescribe.iss`):
- Installs to `%LOCALAPPDATA%\Programs\EDGESCRIBE` (no admin required)
- Adds to user PATH automatically
- Includes all DLLs
- Supports clean uninstall

---

## Migration Notes

### What Changed from onnxruntime-genai (LLM/Vision)

| Aspect | Before (ORT-GenAI) | After (llama.cpp) |
|--------|:---:|:---:|
| LLM model format | ONNX (7 files, 1500 MB) | GGUF (1 file, 990 MB) |
| LLM model loading | `OgaModel::Create()` | `llama_model_load_from_file()` |
| Tokenization | `OgaTokenizer::Encode()` | `llama_tokenize()` |
| Chat template | `OgaTokenizer::ApplyChatTemplate()` | `llama_chat_apply_template()` |
| Generation | `OgaGenerator::GenerateNextToken()` | `llama_sampler_sample()` |
| Token decode | `OgaTokenizerStream::Decode()` | `llama_token_to_piece()` |
| Vision | `OgaMultiModalProcessor::ProcessImages()` | `mtmd_tokenize()` + `mtmd_helper_eval_chunks()` |
| GPU selection | Execution providers | Auto GPU (`n_gpu_layers = 999`), auto-fallback to CPU |
| KV cache | Cleared every turn | **Reused across turns** (prefix matching) |
| Logging | ORT default (verbose) | Suppressed (warnings/errors only) |
| Performance stats | None | TTFT, tok/s, cache hit info |

### What Did NOT Change

- **ASR engine** — still uses onnxruntime-genai (`OgaStreamingProcessor`)
- **TTS engine** — still uses onnxruntime (`Ort::Session`)
- **Server endpoints** — all `/v1/*` REST APIs unchanged
- **CLI commands** — same `chat`, `vision`, `process` interface
- **Public C++ APIs** — `LlmEngine` and `VisionEngine` have identical method signatures

---

## Troubleshooting

### Build Issues

**"Cannot find llama.h"**

Ensure `LLAMA_CPP_PATH` points to the llama.cpp **install** directory. The install
step creates the correct layout:
```bash
cmake --install build --prefix ./install   # creates install/include/llama.h
```

**"Cannot find llama library"**

The CMake build searches these directories:
```
${LLAMA_CPP_PATH}/lib/
${LLAMA_CPP_PATH}/build/src/Release/
${LLAMA_CPP_PATH}/build/Release/
${LLAMA_CPP_PATH}/build/bin/Release/
```

On Windows, ensure the `.lib` import library exists alongside the DLL.

**"CLIP model not found" warning at runtime**

Vision features require a separate mmproj GGUF file. Download it from the same
HuggingFace repo as the main model:
```bash
# Example: download mmproj alongside the main model
curl -L -o models/qwen3-vl/mmproj-model-f16.gguf \
  "https://huggingface.co/Qwen/Qwen3-VL-2B-Instruct-GGUF/resolve/main/mmproj-model-f16.gguf"
```

### Runtime Issues

**Slow inference on CPU**
- Verify CPU supports AVX2: `lscpu | grep avx2` (Linux) or check CPU-Z (Windows)
- Use Q4_K_S quantization for faster inference with slightly lower quality
- Reduce context: modify `n_ctx_` in `llm_engine.cpp` to 2048

**Out of memory**
- Q4_K_M 2B model needs ~2 GB RAM for inference
- Reduce `n_ctx` or try Q2_K quantization
- Close other memory-heavy applications

**GPU not used despite `--device cuda`**
- Verify llama.cpp was built with `DGGML_CUDA=ON`
- Check CUDA toolkit is installed: `nvcc --version`
- Verify GPU is visible: `nvidia-smi`

**macOS Metal not working**
- Verify llama.cpp was built with `DGGML_METAL=ON`
- Metal is automatic on Apple Silicon but must be enabled for Intel Macs
