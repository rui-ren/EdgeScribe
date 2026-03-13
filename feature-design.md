# EDGESCRIBE — Feature Design Document

## Vision

**EDGESCRIBE** is a fully open-source, cross-platform, on-device AI assistant for speech, vision, and language. It runs entirely locally — no cloud, no API keys, no data leaves your machine. Built for privacy-first use cases like HIPAA-compliant medical transcription, meeting notes, document OCR, and accessibility.

Think **Ollama, but for speech-to-text, vision, and clinical AI**.

---

## Architecture Decision Record

### Why C++?

| Factor | C++ | Rust | Python | C# |
|--------|-----|------|--------|----|
| GenAI binding | Native (zero FFI) | 2-3 day FFI wrapper | First-class pip | NuGet |
| Binary size | ~2 MB app | ~3 MB app | ~200 MB bundle | ~50 MB (AOT) |
| Audio capture | miniaudio.h (all platforms) | cpal (all platforms) | sounddevice | NAudio (Windows only) |
| Cross-platform | CMake + CI | cargo + CI | PyInstaller (bloated) | dotnet publish (heavy) |
| Performance | Same (ONNX bottleneck) | Same | Same | Same |
| **Deciding factor** | **onnxruntime-genai IS C++** | FFI overhead for no gain | Poor distribution | Windows-centric audio |

### Why not Foundry Local SDK?

```
EDGESCRIBE (fully open-source)         Foundry Local (partially closed)
─────────────────────────────          ─────────────────────────────────
HuggingFace → ONNX model              Azure AI Foundry → ONNX model
     ↓                                     ↓
onnxruntime-genai (MIT)                SDK (MIT) → Core DLL (closed) → GenAI (MIT)
     ↓                                     ↓
Transcription                          Transcription
```

The Foundry SDK requires the closed-source Core DLL for model management. EDGESCRIBE downloads models directly from HuggingFace, eliminating the proprietary dependency entirely.

### Dependency chain

```
EDGESCRIBE (C++, MIT)
├── onnxruntime-genai (C++, MIT)     — ASR, LLM, VLM inference
├── onnxruntime (C++, MIT)           — TTS inference (Kokoro ONNX)
├── miniaudio.h (C, public domain)   — cross-platform audio capture + playback
└── ONNX models (HuggingFace, open weights)
    ├── nemotron (Parakeet TDT 0.6B)  — ASR
    ├── qwen3-vl-2b (INT4 CPU)        — Vision + Language
    └── kokoro                        — TTS
```

Zero proprietary dependencies. Every component is open-source.

---

## Distribution Model: The Ollama Pattern

EDGESCRIBE follows the **Ollama model** — a single CLI binary that manages models and serves transcription.

### User Experience

```bash
# Install (one command)
# Windows
winget install EDGESCRIBE

# macOS
brew install EDGESCRIBE

# Linux
curl -fsSL https://EDGESCRIBE.dev/install.sh | sh

# --- Usage ---

# Download a model (~600 MB, one-time)
EDGESCRIBE pull nemotron

# Live microphone transcription
EDGESCRIBE run --live

# Transcribe a file
EDGESCRIBE run meeting.wav

# Transcribe and save to file
EDGESCRIBE run meeting.wav -o meeting.txt

# List downloaded models
EDGESCRIBE list

# Start as local API server (for UI/integrations)
EDGESCRIBE serve
```

### Why Ollama-style?

| Feature | Ollama (LLM) | EDGESCRIBE (STT) |
|---------|-------------|-------------------|
| `pull` | Download LLM from ollama.com | Download ASR model from HuggingFace |
| `run` | Chat with model | Transcribe audio |
| `serve` | REST API on localhost | REST API on localhost |
| `list` | Show downloaded models | Show downloaded models |
| Single binary | ✅ | ✅ |
| No Python/Docker | ✅ | ✅ |
| Model registry | ollama.com | HuggingFace (open) |

Users already understand this pattern. No learning curve.

---

## Phased Roadmap

### Phase 1: CLI Tool (MVP) — COMPLETE ✅

**All four engines implemented and building.**

```
EDGESCRIBE/
├── CMakeLists.txt
├── src/
│   ├── cli/main.cpp                # CLI entry point — all commands wired up
│   ├── core/
│   │   └── model_manager.h/cpp     # HuggingFace download + local cache
│   ├── asr/                        # Speech-to-Text
│   │   ├── transcriber.h/cpp       # StreamingProcessor + Generator
│   │   ├── audio_capture.h/cpp     # miniaudio mic capture
│   │   └── audio_file.h/cpp        # WAV file loader (miniaudio decoder)
│   ├── llm/                        # Language Model
│   │   └── llm_engine.h/cpp        # Chat, SOAP notes, summarize, fix terms
│   ├── vision/                     # Vision + OCR
│   │   └── vision_engine.h/cpp     # MultiModalProcessor, OCR, image analysis
│   └── tts/                        # Text-to-Speech
│       └── tts_engine.h/cpp        # Kokoro ONNX via ORT C++ API, playback
├── include/
│   └── miniaudio.h                 # Single-header audio (vendored)
├── installer/
│   └── windows/EDGESCRIBE.iss      # Inno Setup installer (auto PATH)
├── .github/workflows/build.yml     # CI: win-x64, osx-arm64, osx-x64, linux-x64
├── README.md
└── LICENSE (MIT)
```

**Implemented engines**:

| Engine | Class | GenAI API | Status |
|--------|-------|-----------|--------|
| ASR | `Transcriber` | `StreamingProcessor` + `Generator` | ✅ Tested |
| LLM | `LlmEngine` | `Tokenizer` + `Generator` (chat template) | ✅ Implemented |
| Vision | `VisionEngine` | `MultiModalProcessor` + `Generator` | ✅ Implemented |
| TTS | `TtsEngine` | ONNX Runtime C++ API (direct session) | ✅ Implemented |

**All CLI commands**:

| Command | Engine | Status |
|---------|--------|--------|
| `EDGESCRIBE pull <model>` | Core | ✅ |
| `EDGESCRIBE list` | Core | ✅ |
| `EDGESCRIBE remove <model>` | Core | ✅ |
| `EDGESCRIBE run --live` | ASR | ✅ Tested |
| `EDGESCRIBE run <file.wav>` | ASR | ✅ Tested |
| `EDGESCRIBE devices` | ASR | ✅ Tested |
| `EDGESCRIBE chat "prompt"` | LLM | ✅ |
| `EDGESCRIBE vision <image>` | Vision | ✅ |
| `EDGESCRIBE vision <image> --ocr` | Vision | ✅ |
| `EDGESCRIBE process --soap <file>` | LLM/Vision | ✅ |
| `EDGESCRIBE process --summarize <file>` | LLM | ✅ |
| `EDGESCRIBE process --fix-terms <file>` | LLM | ✅ |
| `EDGESCRIBE speak "text"` | TTS | ✅ |
| `EDGESCRIBE speak file.txt -o out.wav` | TTS | ✅ |
| `EDGESCRIBE speak --voices` | TTS | ✅ |

**Supported models**:

| Model | Type | Size | Description |
|-------|------|------|-------------|
| `nemotron` | ASR | ~670 MB | Parakeet TDT 0.6B — real-time English STT |
| `qwen3-vl` | VLM | ~1.5 GB | Qwen3-VL-2B INT4 — vision + language |
| `kokoro` | TTS | ~300 MB | Kokoro ONNX — text-to-speech |
| | | **~2.5 GB** | **Total** |

**Platforms**:

| Platform | Status |
|----------|--------|
| Windows x64 | ✅ Built & tested |
| macOS arm64 (M1/M2/M3) | CI-built |
| macOS x64 | CI-built |
| Linux x64 | CI-built |

**Distribution**:

| Format | Status |
|--------|--------|
| `EDGESCRIBESetup.exe` (Inno Setup) | ✅ Auto-adds to PATH |
| `EDGESCRIBE-win-x64.zip` (portable) | ✅ |
| `EDGESCRIBE-osx-arm64.tar.gz` | CI |
| `EDGESCRIBE-linux-x64.tar.gz` | CI |

---

### Phase 2: Local API Server

**Goal**: `EDGESCRIBE serve` exposes a local REST API for integrations.

```bash
EDGESCRIBE serve --port 8080
```

```
POST /v1/transcribe/stream
  → WebSocket: sends partial transcripts in real-time
  ← Client sends raw PCM audio chunks

POST /v1/transcribe/file
  → Upload WAV, get full transcript
  ← JSON: {"text": "...", "duration": 12.3}

GET /v1/models
  → List available models

POST /v1/models/pull
  → Download model from HuggingFace
```

**Implementation**: Lightweight embedded HTTP server using `cpp-httplib` (already vendored).

**Why**: Enables any UI (web, Electron, native) to use EDGESCRIBE as a backend. Decouples transcription engine from presentation.

---

### Phase 3: Desktop App / UI

Two options — decide later based on community interest:

#### Option A: System Tray App + Web UI (like Ollama)

```
┌──────────────────────────────────────────────┐
│  System Tray Icon                             │
│  ├── Start/Stop Listening                     │
│  ├── Open Dashboard (browser)                 │
│  ├── Settings                                 │
│  └── Quit                                     │
└──────────────────────────────────────────────┘
         │
         ▼  localhost:8080
┌──────────────────────────────────────────────┐
│  Web Dashboard (HTML/JS, served by EDGESCRIBE)│
│                                               │
│  🎤 Live Transcription                        │
│  ┌─────────────────────────────────────────┐  │
│  │ Dr. Smith: The patient presents with... │  │
│  │ [transcript scrolling in real-time]     │  │
│  └─────────────────────────────────────────┘  │
│                                               │
│  📁 File Transcription                        │
│  [Drop WAV file here]                         │
│                                               │
│  📊 History                                   │
│  - meeting_2026-03-12.txt  (2.3 min)         │
│  - patient_notes.txt       (5.1 min)         │
│                                               │
│  ⚙️ Settings                                  │
│  - Model: nemotron ✓ downloaded               │
│  - Audio device: Default Microphone           │
└──────────────────────────────────────────────┘
```

**Pros**: No framework dependency, works everywhere, users already have browsers.
**Cons**: Tray icon requires platform-specific code.

#### Option B: Standalone Desktop App (Electron/Tauri)

- **Tauri** (Rust-based, ~5 MB) wraps the web UI into a native window
- Bundled with EDGESCRIBE C++ backend
- Single installer: `EDGESCRIBE-setup.exe` / `EDGESCRIBE.dmg`

**Pros**: Feels like a native app, auto-updates.
**Cons**: Extra build complexity.

#### Recommendation

Start with **Option A** (tray + web UI). It's simpler to build and more flexible. Tauri wrapper can come later if users want a "real" desktop app.

---

### Phase 4: Package Manager Distribution

| Platform | Package Manager | Install Command |
|----------|----------------|-----------------|
| Windows | winget | `winget install EDGESCRIBE` |
| Windows | Chocolatey | `choco install EDGESCRIBE` |
| macOS | Homebrew | `brew install EDGESCRIBE` |
| Linux | apt (deb) | `sudo apt install EDGESCRIBE` |
| Linux | snap | `snap install EDGESCRIBE` |
| All | GitHub Releases | Download zip/tar.gz |

**winget** requires a manifest PR to [microsoft/winget-pkgs](https://github.com/microsoft/winget-pkgs). Automate via GitHub Actions on release.

---

## Model Registry Design

### Model Stack

| Model | Type | Size | Purpose |
|-------|------|------|---------|
| `nemotron` | ASR | ~670 MB | Real-time speech-to-text (Parakeet TDT 0.6B) |
| `qwen3-vl` | VLM | ~1.5 GB | Vision + language — OCR, SOAP notes, reasoning (Qwen3-VL-2B INT4) |
| `kokoro` | TTS | ~300 MB | Text-to-speech — natural voice output |
| **Total** | | **~2.5 GB** | **Full on-device AI suite** |

All models are quantized (INT4) for CPU. No GPU required.

### HuggingFace Organization

Create a HuggingFace organization: `EDGESCRIBE/`

```
huggingface.co/EDGESCRIBE/
├── nemotron-onnx-cpu/              # ASR: Parakeet TDT 0.6B
│   ├── encoder.onnx
│   ├── decoder.onnx
│   ├── joint.onnx
│   ├── genai_config.json
│   ├── audio_processor_config.json
│   └── tokenizer.json
├── qwen3-vl-2b-onnx-cpu-int4/      # VLM: Qwen3-VL-2B quantized
│   ├── model.onnx
│   ├── model.onnx.data
│   ├── genai_config.json
│   ├── tokenizer.json
│   ├── tokenizer_config.json
│   ├── special_tokens_map.json
│   └── preprocessor_config.json
└── kokoro-onnx/                     # TTS: Kokoro
    ├── model.onnx
    ├── voices.bin
    └── config.json
```

### Model Manifest

EDGESCRIBE ships with a built-in model manifest (can be updated via GitHub):

```json
{
  "models": {
    "nemotron": {
      "name": "Parakeet TDT 0.6B",
      "repo": "EDGESCRIBE/nemotron-onnx-cpu",
      "size_mb": 670,
      "type": "asr",
      "description": "Real-time English speech-to-text. CPU optimized."
    },
    "qwen3-vl": {
      "name": "Qwen3-VL-2B INT4",
      "repo": "EDGESCRIBE/qwen3-vl-2b-onnx-cpu-int4",
      "size_mb": 1500,
      "type": "vlm",
      "description": "Vision + language. OCR, image understanding, SOAP notes."
    },
    "kokoro": {
      "name": "Kokoro TTS",
      "repo": "EDGESCRIBE/kokoro-onnx",
      "size_mb": 300,
      "type": "tts",
      "description": "Text-to-speech. Natural voices. CPU optimized."
    }
  }
}
```

### User Workflows

```bash
# Download all models (~2.5 GB total)
EDGESCRIBE pull nemotron       # ASR
EDGESCRIBE pull qwen3-vl       # Vision + Language
EDGESCRIBE pull kokoro         # TTS

# Doctor dictation → SOAP notes
EDGESCRIBE run --live -o transcript.txt
EDGESCRIBE process --soap transcript.txt

# OCR a prescription photo
EDGESCRIBE vision prescription.jpg --prompt "Extract medications and dosages"

# Read back notes
EDGESCRIBE speak soap_notes.txt
```

### Download Flow

```
EDGESCRIBE pull nemotron
  │
  ├── Read manifest → repo = "EDGESCRIBE/nemotron-onnx-cpu"
  ├── Download each file with progress
  │     encoder.onnx ... done
  │     decoder.onnx ... done
  │     joint.onnx   ... done
  ├── Verify file integrity
  └── Cache to ~/.EDGESCRIBE/models/nemotron/
```

### Local Cache Structure

```
~/.EDGESCRIBE/
├── config.json              # user settings
└── models/
    ├── nemotron/             # ASR
    ├── qwen3-vl/             # Vision + Language
    └── kokoro/               # TTS
```

---

## Cross-Platform Build Strategy

### Native Libraries per Platform

onnxruntime-genai publishes prebuilt binaries. CMake downloads the correct one:

| Platform | GenAI Package | Audio Backend (miniaudio) |
|----------|--------------|--------------------------|
| Windows x64 | `onnxruntime-genai-win-x64.zip` | WASAPI (built-in) |
| macOS arm64 | `onnxruntime-genai-osx-arm64.tar.gz` | CoreAudio (built-in) |
| macOS x64 | `onnxruntime-genai-osx-x64.tar.gz` | CoreAudio (built-in) |
| Linux x64 | `onnxruntime-genai-linux-x64.tar.gz` | PulseAudio / ALSA |

miniaudio.h auto-detects the audio backend at compile time. No configuration needed.

### GitHub Actions CI

```yaml
name: Build & Release

on:
  push:
    tags: ['v*']

jobs:
  build:
    strategy:
      matrix:
        include:
          - os: windows-latest
            rid: win-x64
            ext: .zip
          - os: macos-14
            rid: osx-arm64
            ext: .tar.gz
          - os: macos-13
            rid: osx-x64
            ext: .tar.gz
          - os: ubuntu-22.04
            rid: linux-x64
            ext: .tar.gz

    runs-on: ${{ matrix.os }}

    steps:
      - uses: actions/checkout@v4

      - name: Configure
        run: cmake -B build -DCMAKE_BUILD_TYPE=Release

      - name: Build
        run: cmake --build build --config Release

      - name: Package
        run: cmake --build build --target package

      - name: Upload Release
        uses: softprops/action-gh-release@v2
        with:
          files: build/EDGESCRIBE-${{ matrix.rid }}${{ matrix.ext }}
```

### Release Artifacts

Each GitHub Release contains:

```
EDGESCRIBE v0.1.0
├── EDGESCRIBE-win-x64.zip          (~80 MB)
├── EDGESCRIBE-osx-arm64.tar.gz     (~70 MB)
├── EDGESCRIBE-osx-x64.tar.gz       (~70 MB)
├── EDGESCRIBE-linux-x64.tar.gz     (~70 MB)
└── checksums.sha256
```

Model (~600 MB) is downloaded separately on first run. Install size is small.

---

## Security & Privacy

| Concern | How EDGESCRIBE handles it |
|---------|--------------------------|
| **Data privacy** | All processing is on-device. No network calls during transcription. |
| **HIPAA compliance** | No PHI leaves the machine. No cloud dependency. |
| **Model integrity** | SHA256 checksum verification on download. |
| **Network calls** | Only for `EDGESCRIBE pull` (model download from HuggingFace). |
| **Telemetry** | None. Zero telemetry, zero analytics. |
| **Open source** | Full source available. Auditable by anyone. |

---

## Future Considerations

| Feature | Priority | Notes |
|---------|----------|-------|
| **Proper G2P for TTS** | **High** | espeak-ng or misaki ONNX for correct phoneme tokenization |
| Speaker diarization | Medium | "Speaker 1: ... Speaker 2: ..." |
| Timestamps | Medium | Word-level timestamps for subtitle generation |
| Multiple languages | Medium | ASR models for other languages |
| GPU acceleration | Low | CUDA/DirectML/Metal via ORT execution providers |
| SRT/VTT export | Medium | Subtitle file formats |
| Real-time translation | Low | STT → translate → display |
| Continuous recording | Medium | Hours-long recording sessions |
| Hotkey (push-to-talk) | Medium | Global keyboard shortcut |
| Fine-tuned clinical ASR | Medium | Synthetic data via Gemini + NeMo fine-tuning |

### Vision/VLM + LLM + TTS — IMPLEMENTED ✅

All three additional engines are implemented using the same onnxruntime-genai / onnxruntime pipeline:

**LLM Engine** (`src/llm/llm_engine.h/cpp`):
- `Chat()` — single-turn and multi-turn with chat template
- `GenerateSOAPNotes()` — transcript → structured SOAP
- `Summarize()` — condense text
- `FixMedicalTerms()` — correct medical vocabulary errors

**Vision Engine** (`src/vision/vision_engine.h/cpp`):
- `Analyze()` — image + prompt → response (supports Qwen-VL, Phi-3V, Phi-4)
- `OCR()` — image → extracted text
- `GenerateSOAPNotes()` — transcript + image → SOAP notes
- `DescribeMedicalImage()` — X-ray/chart → description

**TTS Engine** (`src/tts/tts_engine.h/cpp`):
- `Synthesize()` — text → float32 PCM audio
- `SynthesizeToFile()` — text → WAV file
- `Speak()` — text → speakers (miniaudio playback)
- `ListVoices()` — list available voices
- Uses simplified character tokenizer (future: espeak-ng or misaki G2P)

```bash
# LLM
EDGESCRIBE chat "What medications interact with metformin?"
EDGESCRIBE process --soap transcript.txt
EDGESCRIBE process --summarize notes.txt
EDGESCRIBE process --fix-terms transcript.txt

# Vision
EDGESCRIBE vision prescription.jpg --ocr
EDGESCRIBE vision xray.jpg --prompt "Describe findings"
EDGESCRIBE process --soap transcript.txt --image xray.jpg

# TTS
EDGESCRIBE speak "The patient presents with acute bronchitis."
EDGESCRIBE speak soap_notes.txt -o readback.wav
EDGESCRIBE speak --voices
```

---

## Summary

```
Phase 1 (COMPLETE):  CLI tool — ASR, LLM, Vision, TTS all implemented
Phase 2 (next):      Local API server — EDGESCRIBE serve
Phase 3:             System tray + web dashboard UI
Phase 4:             winget / brew / apt distribution

Stack:             C++ + onnxruntime-genai + onnxruntime + miniaudio.h
Models:            HuggingFace (open weights, ONNX format, INT4 quantized)
Platforms:         Windows x64, macOS arm64/x64, Linux x64
Total model size:  ~2.5 GB (nemotron + qwen3-vl + kokoro)
License:           MIT (fully open source)
Privacy:           100% on-device, zero telemetry
```
