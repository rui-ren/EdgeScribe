# Copilot Instructions — EDGESCRIBE

## Build

```bash
# Prerequisites: CMake 3.18+, C++20 compiler, onnxruntime-genai libs

# Download miniaudio (single-header, not committed to repo)
curl -sL -o include/miniaudio.h https://raw.githubusercontent.com/mackron/miniaudio/master/miniaudio.h

# Configure (ORT_GENAI_PATH is required — points to dir with include/ and lib/)
cmake -B build -DCMAKE_BUILD_TYPE=Release -DORT_GENAI_PATH=/path/to/onnxruntime-genai

# Build
cmake --build build --config Release
```

There are no tests or linters configured in this project.

## Architecture

EDGESCRIBE is an on-device AI CLI tool (C++20) with four independent inference engines, a model manager, and a REST API server — all linked into a single binary. No cloud dependencies.

### Engine stack

| Engine | Module | Model | Runtime |
|--------|--------|-------|---------|
| ASR (speech-to-text) | `src/asr/` | Nemotron (Parakeet TDT 0.6B) | onnxruntime-genai |
| LLM (chat/SOAP/summarize) | `src/llm/` | Qwen3-VL text mode | onnxruntime-genai |
| Vision (image/OCR) | `src/vision/` | Qwen3-VL vision mode | onnxruntime-genai |
| TTS (text-to-speech) | `src/tts/` | Kokoro ONNX | onnxruntime (C++ API) |

Engines are **independent** — no shared state. Data flows between them explicitly via the CLI or API layer (e.g., a transcript string is passed from ASR output to LLM input for SOAP notes).

### Data flow

```
🎤 ASR:    Mic/WAV → AudioCapture/AudioFile → Transcriber (StreamingProcessor → Generator) → text
🧠 LLM:    Prompt → Tokenizer → Generator → TokenizerStream → text
🖼️ Vision: Image+Prompt → MultiModalProcessor → Generator → text
🔊 TTS:    Text → Phonemizer (espeak-ng) → Kokoro ONNX Session → PCM → speaker/WAV
```

### Key modules

- **`src/cli/main.cpp`** — CLI entry point. Routes commands (`pull`, `run`, `chat`, `vision`, `speak`, `serve`, `process`) via if/else string matching. Arguments parsed manually.
- **`src/core/model_manager.h/cpp`** — Downloads models from HuggingFace, caches locally. Model manifest is hardcoded in `model_manager.cpp`. Cache: `%LOCALAPPDATA%\EDGESCRIBE\models` (Windows) or `~/.EDGESCRIBE/models` (Unix). Override with `EDGESCRIBE_MODEL_DIR` env var.
- **`src/server/api_server.h/cpp`** — REST API using `httplib.h` (header-only). All endpoints under `/v1/`. JSON is hand-built with helper functions (no JSON library). Serves the web UI from `www/`.
- **`src/asr/diarizer.h/cpp`** — Speaker diarization using ECAPA-TDNN embeddings (192-dim vectors, cosine similarity threshold 0.65).
- **`src/tts/phonemizer.h/cpp`** — Dynamically loads espeak-ng at runtime for grapheme-to-phoneme conversion.

### Web frontend (`www/`)

Vanilla HTML/CSS/JS — no framework, no build step. ES6 modules (`js/app.js`, `js/api.js`). The API server serves these files directly. `api.js` is a thin `fetch()` wrapper over the `/v1/` REST endpoints.

### Header-only dependencies (`include/`)

- **`httplib.h`** — HTTP client/server for the REST API
- **`miniaudio.h`** — Cross-platform audio capture/playback (downloaded at build time, gitignored)

## Conventions

### C++ style

- **Namespace**: `EDGESCRIBE` top-level, with sub-namespaces `EDGESCRIBE::asr`, `EDGESCRIBE::llm`, `EDGESCRIBE::vision`, `EDGESCRIBE::tts`, `EDGESCRIBE::server`.
- **Header guards**: `#pragma once` (all headers).
- **Pimpl pattern**: All engine classes use `struct Impl; std::unique_ptr<Impl> impl_;` to hide implementation details. Destructors declared in headers, defined in `.cpp` files as `= default`.
- **No copy**: Engine classes delete copy constructor and copy assignment.
- **Callbacks over inheritance**: Async results delivered via `std::function` callbacks (`TranscriptCallback`, `TokenCallback`, `ProgressCallback`, `AudioCallback`) rather than virtual methods.
- **Error handling**: Exceptions only (`std::runtime_error`). Caught at the top-level CLI. No error codes or Result types.
- **Logging**: `std::cout` / `std::cerr` directly. No logging framework.
- **Data structs**: Plain structs with public fields for data containers (`ModelInfo`, `TimestampedWord`, `SpeakerSegment`, `ChatMessage`, `AudioOutput`, `ServerConfig`).
- **Constants**: `static constexpr` in headers (e.g., `kChunkSize`, `kSampleRate`).

### Platform-specific code

Use `#ifdef _WIN32` / `#else` blocks. Key platform differences:
- File paths and model cache directories
- HTTP download: PowerShell (`Invoke-WebRequest`) on Windows, `curl` on Unix
- Audio backends: WinMM on Windows, CoreAudio on macOS, PulseAudio/ALSA on Linux
- CMakeLists.txt links platform-specific libs (`ws2_32`/`winmm` on Windows, CoreAudio frameworks on macOS)

### JSON handling

No JSON library. The server uses hand-rolled helpers: `JsonEscape()`, `JsonObj()`, `ExtractJsonString()`. Keep this pattern when adding new endpoints.

### Model integration

When adding support for a new model:
1. Add the model entry to the manifest in `model_manager.cpp` (name, HuggingFace repo, file list, config file)
2. Create or extend an engine class in the appropriate module
3. Add CLI command routing in `main.cpp`
4. Add REST endpoint in `api_server.cpp` if needed
