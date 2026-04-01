# EDGESCRIBE

**On-device AI for speech, vision, and language. Private. Open source.**

EDGESCRIBE is a fully open-source, cross-platform AI assistant that runs entirely on your device. Speech-to-text, image understanding, document OCR, and text-to-speech — all local, all private.

Built for privacy-first use cases like HIPAA-compliant medical transcription, meeting notes, and accessibility.

## Features

- **100% on-device** — All processing happens locally. Zero network calls during use.
- **GPU accelerated** — Vulkan (Windows, any GPU) and Metal (macOS). Auto-detects GPU, falls back to CPU.
- **Cross-platform** — Windows x64 and macOS Apple Silicon (M1/M2/M3/M4). Linux and macOS Intel planned.
- **4-engine AI stack** — ASR + Vision + LLM + TTS in only ~1.7 GB.
- **Real-time transcription** — Live microphone to text with streaming output.
- **Vision & OCR** — Analyze images, extract text from documents.
- **LLM chat** — Local language model for SOAP notes, summaries, terminology fixes.
- **Text-to-speech** — Natural voice output via Kokoro ONNX.
- **Ollama-style CLI** — Simple `pull` / `run` / `list` commands.
- **Small footprint** — ~2 MB app + ~80 MB runtime. Models downloaded separately.
- **CPU-capable** — No GPU required. Runs on any modern laptop.
- **MIT licensed** — Fully open source. Auditable by anyone.

## Why Native C++? Performance by Design

EDGESCRIBE is built as a **single native C++ binary** where the HTTP server, inference engines, and audio pipeline all run **in the same process**. This is a deliberate architectural choice for maximum edge performance.

### How EDGESCRIBE compares

| | EDGESCRIBE (C++) | Ollama (Go) | LM Studio (Electron) | Jan.ai (Tauri) |
|---|---|---|---|---|
| **Inference engine** | llama.cpp + ONNX Runtime | llama.cpp | llama.cpp | llama.cpp (via Cortex.cpp) |
| **Server / shell** | C++ (httplib.h) | Go HTTP server | Node.js (Electron) | Rust shell (Tauri) + C++ (Cortex) |
| **Frontend** | Vanilla HTML/JS | None (CLI) | React (Electron) | React (Tauri) |
| **FFI boundary** | ✅ None — all C++ | ⚠️ CGO bridge | ⚠️ Node.js ↔ C++ | ⚠️ TS ↔ C++ |
| **Binary size** | ~2 MB | ~30 MB | ~400 MB | ~80 MB |
| **RAM overhead** | ~5 MB (no runtime) | ~30 MB (Go) | ~300 MB (Chromium) | ~50 MB (Tauri) |
| **GC pauses** | ✅ None | ⚠️ Go GC | ⚠️ V8 GC | ⚠️ V8 GC (webview) |
| **Multi-modal** | ✅ ASR + LLM + Vision + TTS | Text + Vision | Text + Vision | Text + Vision |
| **Focus** | Medical / domain-specific | Developer API | Consumer chat UI | "Open ChatGPT" |
| **Deployment** | Single binary, zero deps | Single binary | Installer | Installer |

### Why this matters on your laptop

Most local AI tools (Ollama, LM Studio, etc.) use Go or Python servers that call into C/C++ inference libraries through a **foreign function interface (FFI)**. Every token, every audio chunk, every image crosses a language boundary — adding latency and memory overhead.

EDGESCRIBE eliminates this entirely. The HTTP server ([cpp-httplib](https://github.com/yhirose/cpp-httplib)) and inference engines ([llama.cpp](https://github.com/ggerganov/llama.cpp) for LLM/Vision, [ONNX Runtime](https://github.com/microsoft/onnxruntime) for ASR/TTS) share the same address space. Inference output flows directly to the HTTP response with **zero serialization, zero copies, zero boundary crossings**.

The result: lower latency, lower memory usage, and smoother real-time streaming — especially noticeable on laptops without dedicated GPUs.

## Quick Start

### 1. Download

Grab the latest release for your platform from [Releases](https://github.com/EDGESCRIBE/EDGESCRIBE/releases):

| Platform | Download |
|----------|----------|
| Windows x64 | `EDGESCRIBE-win-x64.zip` |
| macOS Apple Silicon | `EDGESCRIBE-osx-arm64.tar.gz` |

### 2. Download Models

```bash
# Speech-to-text (required, ~670 MB)
edgescribe pull nemotron

# Vision + language — OCR, SOAP notes (optional, ~990 MB)
edgescribe pull qwen3-vl

# Text-to-speech (optional, ~300 MB)
edgescribe pull kokoro
```

### 3. Transcribe

```bash
# Live microphone transcription
edgescribe run --live

# Transcribe a WAV file
edgescribe run meeting.wav

# Save transcript to file
edgescribe run meeting.wav -o transcript.txt
```

## Commands

### Model Management

| Command | Description |
|---------|-------------|
| `edgescribe pull <model>` | Download a model from HuggingFace |
| `edgescribe list` | List available and downloaded models |
| `edgescribe remove <model>` | Delete a downloaded model |

### Speech-to-Text (ASR)

| Command | Description |
|---------|-------------|
| `edgescribe run --live` | Live microphone transcription |
| `edgescribe run <file.wav>` | Transcribe a WAV file |
| `edgescribe run <file> -o out.txt` | Transcribe and save to file |
| `edgescribe devices` | List audio input devices |

```bash
# Live transcription — speaks into mic, text streams to terminal
edgescribe run --live

# Live transcription with a specific model path
edgescribe run --live --model /path/to/custom/model

# Transcribe a WAV file
edgescribe run meeting.wav

# Transcribe and save output
edgescribe run meeting.wav -o transcript.txt

# Use a specific model
edgescribe run meeting.wav --model nemotron
```

### Vision & OCR

| Command | Description |
|---------|-------------|
| `edgescribe vision <image>` | Describe an image |
| `edgescribe vision <image> --prompt "..."` | Analyze with custom prompt |
| `edgescribe vision <image> --ocr` | Extract text from image (OCR) |
| `edgescribe vision <image> -o out.txt` | Save analysis to file |

```bash
# Describe an image
edgescribe vision photo.jpg

# OCR — extract text from a document photo
edgescribe vision prescription.jpg --ocr

# Analyze with a specific prompt
edgescribe vision xray.jpg --prompt "Describe any abnormalities"

# Save output to file
edgescribe vision chart.jpg --ocr -o extracted.txt

# Use a specific model
edgescribe vision scan.png --model /path/to/qwen3-vl
```

### LLM Chat

| Command | Description |
|---------|-------------|
| `edgescribe chat` | Interactive multi-turn chat (type `/exit` to quit) |
| `edgescribe chat "<prompt>"` | Single-turn chat with the local language model |
| `edgescribe chat "<prompt>" -o out.txt` | Save response to file |

```bash
# Interactive multi-turn chat (remembers conversation context)
edgescribe chat
# You: What are the side effects of metformin?
# Assistant: Common side effects include nausea, diarrhea...
# You: What about for patients with kidney disease?
# Assistant: For patients with renal impairment, metformin...
# You: /exit

# Single-turn question
edgescribe chat "What are the side effects of metformin?"

# Save response
edgescribe chat "Summarize diabetes treatment guidelines" -o summary.txt

# Save full conversation from interactive mode
edgescribe chat -o conversation.txt
```

### Post-Processing

| Command | Description |
|---------|-------------|
| `edgescribe process --soap <file>` | Generate SOAP notes from transcript |
| `edgescribe process --summarize <file>` | Summarize text |
| `edgescribe process --fix-terms <file>` | Fix medical terminology errors |
| `edgescribe process --soap <file> --image <img>` | SOAP notes with image context |

```bash
# Generate SOAP notes from a transcript
edgescribe process --soap transcript.txt

# Generate SOAP notes with an attached image
edgescribe process --soap transcript.txt --image xray.jpg

# Summarize a document
edgescribe process --summarize meeting_notes.txt

# Fix medical terminology in a transcript
edgescribe process --fix-terms raw_transcript.txt

# Save output
edgescribe process --soap transcript.txt -o soap_notes.txt
```

### Text-to-Speech

| Command | Description |
|---------|-------------|
| `edgescribe speak "<text>"` | Read text aloud through speakers |
| `edgescribe speak <file.txt>` | Read a text file aloud |
| `edgescribe speak "<text>" -o out.wav` | Save speech to WAV file |
| `edgescribe speak --voices` | List available voices |

```bash
# Speak text aloud
edgescribe speak "The patient presents with acute bronchitis."

# Read a file aloud
edgescribe speak soap_notes.txt

# Save to WAV file
edgescribe speak "Hello world" -o greeting.wav

# Use a specific voice and speed
edgescribe speak "Hello" --voice af_heart --speed 1.2

# List available voices
edgescribe speak --voices
```

## Global Options

| Option | Short | Description | Default |
|--------|-------|-------------|---------|
| `--model <name\|path>` | `-m` | Model to use | `nemotron` (ASR), `qwen3-vl` (vision/chat), `kokoro` (TTS) |
| `-o <file>` | `--output` | Write output to file | stdout |
| `--prompt <text>` | `-p` | Custom prompt (vision) | "Describe this image in detail." |
| `--voice <name>` | `-v` | TTS voice name | `af_heart` |
| `--speed <float>` | | TTS speaking speed | `1.0` |
| `--ocr` | | OCR mode (vision) | off |
| `--soap` | | SOAP notes mode (process) | — |
| `--summarize` | | Summarize mode (process) | — |
| `--fix-terms` | | Fix medical terms mode (process) | — |
| `--image <file>` | | Attach image to process (SOAP) | — |
| `--live` | | Live microphone mode (run) | — |
| `--voices` | | List TTS voices (speak) | — |
| `--version` | `-v` | Show version | — |
| `--help` | `-h` | Show help | — |

## Models

| Model | Type | Size | Description |
|-------|------|------|-------------|
| `nemotron` | ASR | ~670 MB | Real-time English speech-to-text (Parakeet TDT 0.6B, ONNX) |
| `qwen3-vl` | VLM | ~990 MB | Vision + language — OCR, SOAP notes (Qwen3-VL-2B Q4_K_M, GGUF) |
| `kokoro` | TTS | ~300 MB | Text-to-speech with natural voices (ONNX) |
| | | **~1.96 GB** | **Total for full AI suite** |

Models are downloaded from HuggingFace and cached locally at:
- **Windows**: `%LOCALAPPDATA%\EDGESCRIBE\models\`
- **macOS/Linux**: `~/.EDGESCRIBE/models/`

Override with `EDGESCRIBE_MODEL_DIR` environment variable.

### Using a Custom Model

You can point to any local model directory (for ONNX models) or GGUF file (for LLM/Vision):

```bash
edgescribe run --live --model /path/to/my/custom/model
edgescribe chat "Hello" --model /path/to/model.gguf
```

## Building from Source

### Prerequisites

- CMake 3.18+
- C++20 compiler (MSVC 2022, Clang 14+, GCC 12+)
- onnxruntime-genai headers and libraries (for ASR + TTS)
- llama.cpp headers and libraries (for LLM + Vision)

See [doc/llama-cpp-integration.md](doc/llama-cpp-integration.md) for detailed setup.

### Build

```bash
# Clone
git clone https://github.com/EDGESCRIBE/EDGESCRIBE.git
cd EDGESCRIBE

# Download miniaudio (single-header audio library)
mkdir -p include
curl -sL -o include/miniaudio.h https://raw.githubusercontent.com/mackron/miniaudio/master/miniaudio.h

# Configure and build
cmake -B build -DCMAKE_BUILD_TYPE=Release \
  -DORT_GENAI_PATH=/path/to/onnxruntime-genai \
  -DLLAMA_CPP_PATH=/path/to/llama.cpp/install
cmake --build build --config Release
```

### Build with local onnxruntime-genai source

If you've built onnxruntime-genai from source:

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release \
  -DORT_GENAI_PATH=/path/to/onnxruntime-genai/build/Windows/Release \
  -DLLAMA_CPP_PATH=/path/to/llama.cpp/install
```

The `ORT_GENAI_PATH` should contain:
- `include/` with `ort_genai.h` and `ort_genai_c.h`
- `lib/` with the native libraries (`.dll` / `.dylib` / `.so`)

The `LLAMA_CPP_PATH` should contain:
- `include/` with `llama.h`
- `lib/` with the llama library

## REST API Server

Start the API server to integrate with any frontend:

```bash
# Start on default port 8080
edgescribe serve

# Custom port
edgescribe serve --port 3000

# Specify which models to load
edgescribe serve --asr-model /path/to/nemotron --vlm-model /path/to/qwen3-vl
```

### API Endpoints

**Health & Info:**

| Method | Endpoint | Description |
|--------|----------|-------------|
| GET | `/v1/health` | Health check + loaded engines |
| GET | `/v1/models` | List loaded models |

**Speech-to-Text:**

| Method | Endpoint | Description |
|--------|----------|-------------|
| POST | `/v1/transcribe/file` | Upload WAV file, get transcript |
| POST | `/v1/transcribe/stream` | Start streaming ASR session |
| POST | `/v1/transcribe/push` | Push raw PCM audio chunk |
| POST | `/v1/transcribe/flush` | Flush and get final transcript |

**LLM Chat:**

| Method | Endpoint | Description |
|--------|----------|-------------|
| POST | `/v1/chat` | Chat completion |
| POST | `/v1/chat/soap` | Generate SOAP notes from transcript |
| POST | `/v1/chat/summarize` | Summarize text |

**Vision:**

| Method | Endpoint | Description |
|--------|----------|-------------|
| POST | `/v1/vision/analyze` | Analyze image with prompt (multipart) |
| POST | `/v1/vision/ocr` | OCR an image (multipart) |

**Text-to-Speech:**

| Method | Endpoint | Description |
|--------|----------|-------------|
| POST | `/v1/tts/synthesize` | Text → WAV audio |
| GET | `/v1/tts/voices` | List available voices |

### API Examples

```bash
# Health check
curl http://localhost:8080/v1/health

# Transcribe a file
curl -X POST http://localhost:8080/v1/transcribe/file \
  -F "audio=@meeting.wav"

# Chat
curl -X POST http://localhost:8080/v1/chat \
  -H "Content-Type: application/json" \
  -d '{"prompt": "What are the side effects of ibuprofen?", "system": "You are a medical assistant."}'

# SOAP notes
curl -X POST http://localhost:8080/v1/chat/soap \
  -H "Content-Type: application/json" \
  -d '{"transcript": "Patient presents with headache for 3 days..."}'

# Vision / OCR
curl -X POST http://localhost:8080/v1/vision/ocr \
  -F "image=@prescription.jpg"

# Text-to-speech (returns WAV)
curl -X POST http://localhost:8080/v1/tts/synthesize \
  -H "Content-Type: application/json" \
  -d '{"text": "Hello world", "voice": "af_heart"}' \
  -o output.wav

# List TTS voices
curl http://localhost:8080/v1/tts/voices
```

### Frontend Integration

The API server binds to `127.0.0.1` (localhost only) and includes CORS headers, so any local web frontend can connect:

```javascript
// JavaScript example
const res = await fetch('http://localhost:8080/v1/chat', {
  method: 'POST',
  headers: { 'Content-Type': 'application/json' },
  body: JSON.stringify({ prompt: 'Hello', system: 'You are helpful.' })
});
const data = await res.json();
console.log(data.text);
```

## Architecture

```
EDGESCRIBE (C++, MIT)
├── llama.cpp             # Inference engine for LLM + Vision (MIT)
├── onnxruntime-genai     # Inference engine for ASR (MIT)
├── onnxruntime           # Inference engine for TTS (MIT)
├── miniaudio.h           # Cross-platform audio capture + playback (Public Domain)
├── GGUF models           # LLM/Vision from HuggingFace (open weights)
└── ONNX models           # ASR/TTS from HuggingFace (open weights)
```

```
src/
├── cli/main.cpp                # CLI entry point — all commands
├── core/model_manager.h/cpp    # HuggingFace model download + cache
├── asr/                        # Speech-to-Text (Nemotron)
│   ├── transcriber.h/cpp       ── StreamingProcessor + Generator pipeline
│   ├── audio_capture.h/cpp     ── Microphone input (miniaudio)
│   └── audio_file.h/cpp        ── WAV file loader
├── llm/                        # Language Model (Qwen3-VL text mode, llama.cpp)
│   └── llm_engine.h/cpp        ── Chat, SOAP notes, summarize, fix terms
├── vision/                     # Vision + OCR (Qwen3-VL vision mode, llama.cpp)
│   └── vision_engine.h/cpp     ── CLIP + llama.cpp, OCR, image analysis
└── tts/                        # Text-to-Speech (Kokoro ONNX)
    └── tts_engine.h/cpp        ── ONNX Runtime C++ API, audio playback
```

### Data Flow

```
🎤 ASR:    Mic/WAV → StreamingProcessor → Generator → TokenizerStream → text  (onnxruntime-genai)
🧠 LLM:    Prompt → llama_tokenize → llama_decode → llama_sampler → text     (llama.cpp)
🖼️ Vision: Image+Prompt → CLIP encoder → llama_decode → text                  (llama.cpp)
🔊 TTS:    Text → Phonemizer → ONNX Runtime Session → PCM audio → speaker    (onnxruntime)
```

## Privacy & Security

- **On-device processing** — Audio never leaves your machine
- **No telemetry** — Zero analytics, zero tracking
- **No cloud dependency** — Works fully offline after model download
- **HIPAA-friendly** — No PHI transmitted, no BAA needed
- **Open source** — Full source code available for audit

## Roadmap

### v1.0 — Core AI Suite (Current)

Ship the foundation: on-device ASR + LLM + Vision + TTS in a single binary, zero cloud dependencies.

- [x] CLI tool with `pull` / `run` / `list` commands
- [x] Live microphone transcription
- [x] WAV file transcription
- [x] Cross-platform builds (Windows x64, macOS ARM64)
- [x] LLM chat (`edgescribe chat`) with multi-turn support
- [x] Vision & OCR (`edgescribe vision`)
- [x] SOAP notes / summarize / fix terms (`edgescribe process`)
- [x] Text-to-speech (`edgescribe speak`)
- [x] Windows installer (Inno Setup, auto-adds to PATH)
- [x] Proper G2P phonemizer for TTS (espeak-ng or misaki ONNX)
- [x] Local REST API server (`edgescribe serve`)
- [x] GPU acceleration — Vulkan (Windows) and Metal (macOS), auto-detect with CPU fallback
- [x] SQLite persistent memory — auto-save chat + process history
- [x] `edgescribe history` — list, show, search, delete past sessions

### v1.1 — Transcription Quality + Performance

Improve ASR output quality and optimize multi-turn chat performance.

- [ ] KV cache reuse for multi-turn chat (skip re-processing cached tokens)
- [ ] DirectML (Windows) / CoreML (macOS) for ASR/TTS acceleration
- [ ] Speaker diarization (who said what)
- [ ] Word-level timestamps
- [ ] SRT/VTT subtitle export
- [ ] Web dashboard UI

### v1.2 — Knowledge Base (RAG)

Let users ingest their own documents (research reports, clinical guidelines, protocols) and query against them. LLM answers grounded in user's content.

- [ ] Document ingestion with text chunking (TXT, CSV, MD)
- [ ] FTS5 search on knowledge base chunks
- [ ] RAG-augmented LLM prompts (auto-retrieve relevant context)
- [ ] `edgescribe kb` CLI commands (add, list, search, remove)
- [ ] REST API endpoints for KB (`/v1/kb/*`)

### v1.3 — Semantic Search

Add a dedicated embedding model for similarity-based retrieval, improving search quality beyond keyword matching.

- [ ] `all-MiniLM-L6-v2` ONNX embedding model (~80 MB, 384-dim vectors)
- [ ] Brute-force cosine similarity search in C++ (<1ms for 10K entries)
- [ ] Embed chunks on ingestion, store as BLOBs in SQLite
- [ ] Hybrid search (FTS5 keywords + cosine similarity)
- [ ] `edgescribe pull embeddings` to download model

### Future

- [ ] CUDA acceleration (NVIDIA-specific optimized build)
- [ ] Multiple languages
- [ ] Package managers (winget, brew, apt)
- [ ] Configurable context window (`--context 8192` / `--context 32768`)
- [ ] KV cache quantization (Q8_0) for lower memory usage
- [ ] PDF/DOCX ingestion for knowledge base
- [ ] Specialty LoRA adapters (cardiology, dermatology, psychiatry, etc.)

## License

MIT — see [LICENSE](LICENSE).

## Acknowledgments

- [llama.cpp](https://github.com/ggerganov/llama.cpp) — LLM and Vision inference (GGUF)
- [ONNX Runtime GenAI](https://github.com/microsoft/onnxruntime-genai) — ASR inference
- [ONNX Runtime](https://github.com/microsoft/onnxruntime) — TTS inference
- [miniaudio](https://github.com/mackron/miniaudio) — Cross-platform audio
- [NVIDIA Parakeet](https://huggingface.co/nvidia/parakeet-tdt-0.6b-v2) — ASR model
- [Qwen3-VL](https://huggingface.co/Qwen/Qwen3-VL-2B-Instruct) — Vision + language model
- [Kokoro TTS](https://github.com/thewh1teagle/kokoro-onnx) — Text-to-speech model
- [misaki G2P](https://huggingface.co/blog/hexgrad/g2p) — Grapheme-to-phoneme (future)
