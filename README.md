# EDGESCRIBE

**On-device AI for speech, vision, and language. Private. Open source.**

EDGESCRIBE is a fully open-source, cross-platform AI assistant that runs entirely on your device. Speech-to-text, image understanding, document OCR, and text-to-speech — all local, all private.

Built for privacy-first use cases like HIPAA-compliant medical transcription, meeting notes, and accessibility.

## Features

- **100% on-device** — All processing happens locally. Zero network calls during use.
- **Cross-platform** — Windows, macOS (Intel & Apple Silicon), Linux.
- **4-engine AI stack** — ASR + Vision + LLM + TTS in only ~2.5 GB.
- **Real-time transcription** — Live microphone to text with streaming output.
- **Vision & OCR** — Analyze images, extract text from documents.
- **LLM chat** — Local language model for SOAP notes, summaries, terminology fixes.
- **Text-to-speech** — Natural voice output via Kokoro ONNX.
- **Ollama-style CLI** — Simple `pull` / `run` / `list` commands.
- **Small footprint** — ~2 MB app + ~80 MB runtime. Models downloaded separately.
- **CPU-capable** — No GPU required. Runs on any modern laptop.
- **MIT licensed** — Fully open source. Auditable by anyone.

## Quick Start

### 1. Download

Grab the latest release for your platform from [Releases](https://github.com/EDGESCRIBE/EDGESCRIBE/releases):

| Platform | Download |
|----------|----------|
| Windows x64 | `EDGESCRIBE-win-x64.zip` |
| macOS Apple Silicon | `EDGESCRIBE-osx-arm64.tar.gz` |
| macOS Intel | `EDGESCRIBE-osx-x64.tar.gz` |
| Linux x64 | `EDGESCRIBE-linux-x64.tar.gz` |

### 2. Download Models

```bash
# Speech-to-text (required, ~670 MB)
EDGESCRIBE pull nemotron

# Vision + language — OCR, SOAP notes (optional, ~1.5 GB)
EDGESCRIBE pull qwen3-vl

# Text-to-speech (optional, ~300 MB)
EDGESCRIBE pull kokoro
```

### 3. Transcribe

```bash
# Live microphone transcription
EDGESCRIBE run --live

# Transcribe a WAV file
EDGESCRIBE run meeting.wav

# Save transcript to file
EDGESCRIBE run meeting.wav -o transcript.txt
```

## Commands

### Model Management

| Command | Description |
|---------|-------------|
| `EDGESCRIBE pull <model>` | Download a model from HuggingFace |
| `EDGESCRIBE list` | List available and downloaded models |
| `EDGESCRIBE remove <model>` | Delete a downloaded model |

### Speech-to-Text (ASR)

| Command | Description |
|---------|-------------|
| `EDGESCRIBE run --live` | Live microphone transcription |
| `EDGESCRIBE run <file.wav>` | Transcribe a WAV file |
| `EDGESCRIBE run <file> -o out.txt` | Transcribe and save to file |
| `EDGESCRIBE devices` | List audio input devices |

```bash
# Live transcription — speaks into mic, text streams to terminal
EDGESCRIBE run --live

# Live transcription with a specific model path
EDGESCRIBE run --live --model /path/to/custom/model

# Transcribe a WAV file
EDGESCRIBE run meeting.wav

# Transcribe and save output
EDGESCRIBE run meeting.wav -o transcript.txt

# Use a specific model
EDGESCRIBE run meeting.wav --model nemotron
```

### Vision & OCR

| Command | Description |
|---------|-------------|
| `EDGESCRIBE vision <image>` | Describe an image |
| `EDGESCRIBE vision <image> --prompt "..."` | Analyze with custom prompt |
| `EDGESCRIBE vision <image> --ocr` | Extract text from image (OCR) |
| `EDGESCRIBE vision <image> -o out.txt` | Save analysis to file |

```bash
# Describe an image
EDGESCRIBE vision photo.jpg

# OCR — extract text from a document photo
EDGESCRIBE vision prescription.jpg --ocr

# Analyze with a specific prompt
EDGESCRIBE vision xray.jpg --prompt "Describe any abnormalities"

# Save output to file
EDGESCRIBE vision chart.jpg --ocr -o extracted.txt

# Use a specific model
EDGESCRIBE vision scan.png --model /path/to/qwen3-vl
```

### LLM Chat

| Command | Description |
|---------|-------------|
| `EDGESCRIBE chat "<prompt>"` | Chat with the local language model |
| `EDGESCRIBE chat "<prompt>" -o out.txt` | Save response to file |

```bash
# Ask a question
EDGESCRIBE chat "What are the side effects of metformin?"

# Save response
EDGESCRIBE chat "Summarize diabetes treatment guidelines" -o summary.txt

# Use a specific model
EDGESCRIBE chat "Hello" --model /path/to/model
```

### Post-Processing

| Command | Description |
|---------|-------------|
| `EDGESCRIBE process --soap <file>` | Generate SOAP notes from transcript |
| `EDGESCRIBE process --summarize <file>` | Summarize text |
| `EDGESCRIBE process --fix-terms <file>` | Fix medical terminology errors |
| `EDGESCRIBE process --soap <file> --image <img>` | SOAP notes with image context |

```bash
# Generate SOAP notes from a transcript
EDGESCRIBE process --soap transcript.txt

# Generate SOAP notes with an attached image
EDGESCRIBE process --soap transcript.txt --image xray.jpg

# Summarize a document
EDGESCRIBE process --summarize meeting_notes.txt

# Fix medical terminology in a transcript
EDGESCRIBE process --fix-terms raw_transcript.txt

# Save output
EDGESCRIBE process --soap transcript.txt -o soap_notes.txt
```

### Text-to-Speech

| Command | Description |
|---------|-------------|
| `EDGESCRIBE speak "<text>"` | Read text aloud through speakers |
| `EDGESCRIBE speak <file.txt>` | Read a text file aloud |
| `EDGESCRIBE speak "<text>" -o out.wav` | Save speech to WAV file |
| `EDGESCRIBE speak --voices` | List available voices |

```bash
# Speak text aloud
EDGESCRIBE speak "The patient presents with acute bronchitis."

# Read a file aloud
EDGESCRIBE speak soap_notes.txt

# Save to WAV file
EDGESCRIBE speak "Hello world" -o greeting.wav

# Use a specific voice and speed
EDGESCRIBE speak "Hello" --voice af_heart --speed 1.2

# List available voices
EDGESCRIBE speak --voices
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
| `nemotron` | ASR | ~670 MB | Real-time English speech-to-text (Parakeet TDT 0.6B) |
| `qwen3-vl` | VLM | ~1.5 GB | Vision + language — OCR, SOAP notes (Qwen3-VL-2B INT4) |
| `kokoro` | TTS | ~300 MB | Text-to-speech with natural voices |
| | | **~2.5 GB** | **Total for full AI suite** |

Models are downloaded from HuggingFace and cached locally at:
- **Windows**: `%LOCALAPPDATA%\EDGESCRIBE\models\`
- **macOS/Linux**: `~/.EDGESCRIBE/models/`

Override with `EDGESCRIBE_MODEL_DIR` environment variable.

### Using a Custom Model

You can point to any local model directory containing a `genai_config.json`:

```bash
EDGESCRIBE run --live --model /path/to/my/custom/model
```

## Building from Source

### Prerequisites

- CMake 3.18+
- C++20 compiler (MSVC 2022, Clang 14+, GCC 12+)
- onnxruntime-genai headers and libraries

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
  -DORT_GENAI_PATH=/path/to/onnxruntime-genai
cmake --build build --config Release
```

### Build with local onnxruntime-genai source

If you've built onnxruntime-genai from source:

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release \
  -DORT_GENAI_PATH=/path/to/onnxruntime-genai/build/Windows/Release
```

The `ORT_GENAI_PATH` should contain:
- `include/` with `ort_genai.h` and `ort_genai_c.h`
- `lib/` with the native libraries (`.dll` / `.dylib` / `.so`)

## REST API Server

Start the API server to integrate with any frontend:

```bash
# Start on default port 8080
EDGESCRIBE serve

# Custom port
EDGESCRIBE serve --port 3000

# Specify which models to load
EDGESCRIBE serve --asr-model /path/to/nemotron --vlm-model /path/to/qwen3-vl
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
├── onnxruntime-genai     # Inference engine for ASR, LLM, VLM (MIT)
├── onnxruntime           # Inference engine for TTS (MIT)
├── miniaudio.h           # Cross-platform audio capture + playback (Public Domain)
└── ONNX models           # From HuggingFace (open weights)
```

```
src/
├── cli/main.cpp                # CLI entry point — all commands
├── core/model_manager.h/cpp    # HuggingFace model download + cache
├── asr/                        # Speech-to-Text (Nemotron)
│   ├── transcriber.h/cpp       ── StreamingProcessor + Generator pipeline
│   ├── audio_capture.h/cpp     ── Microphone input (miniaudio)
│   └── audio_file.h/cpp        ── WAV file loader
├── llm/                        # Language Model (Qwen3-VL text mode)
│   └── llm_engine.h/cpp        ── Chat, SOAP notes, summarize, fix terms
├── vision/                     # Vision + OCR (Qwen3-VL vision mode)
│   └── vision_engine.h/cpp     ── MultiModalProcessor, OCR, image analysis
└── tts/                        # Text-to-Speech (Kokoro ONNX)
    └── tts_engine.h/cpp        ── ONNX Runtime C++ API, audio playback
```

### Data Flow

```
🎤 ASR:    Mic/WAV → StreamingProcessor → Generator → TokenizerStream → text
🧠 LLM:    Prompt → Tokenizer → Generator → TokenizerStream → text
🖼️ Vision: Image+Prompt → MultiModalProcessor → Generator → text
🔊 TTS:    Text → Tokenizer → ONNX Runtime Session → PCM audio → speaker/WAV
```

## Privacy & Security

- **On-device processing** — Audio never leaves your machine
- **No telemetry** — Zero analytics, zero tracking
- **No cloud dependency** — Works fully offline after model download
- **HIPAA-friendly** — No PHI transmitted, no BAA needed
- **Open source** — Full source code available for audit

## Roadmap

- [x] CLI tool with `pull` / `run` / `list` commands
- [x] Live microphone transcription
- [x] WAV file transcription
- [x] Cross-platform builds (Windows, macOS, Linux)
- [x] LLM chat (`EDGESCRIBE chat`)
- [x] Vision & OCR (`EDGESCRIBE vision`)
- [x] SOAP notes / summarize / fix terms (`EDGESCRIBE process`)
- [x] Text-to-speech (`EDGESCRIBE speak`)
- [x] Windows installer (Inno Setup, auto-adds to PATH)
- [x] Proper G2P phonemizer for TTS (espeak-ng or misaki ONNX)
- [x] Local REST API server (`EDGESCRIBE serve`)
- [ ] Web dashboard UI
- [ ] Speaker diarization
- [ ] Word-level timestamps
- [ ] SRT/VTT subtitle export
- [ ] GPU acceleration (CUDA, DirectML, Metal)
- [ ] Multiple languages
- [ ] Package managers (winget, brew, apt)

## License

MIT — see [LICENSE](LICENSE).

## Acknowledgments

- [ONNX Runtime GenAI](https://github.com/microsoft/onnxruntime-genai) — ASR, LLM, VLM inference
- [ONNX Runtime](https://github.com/microsoft/onnxruntime) — TTS inference
- [miniaudio](https://github.com/mackron/miniaudio) — Cross-platform audio
- [NVIDIA Parakeet](https://huggingface.co/nvidia/parakeet-tdt-0.6b-v2) — ASR model
- [Qwen3-VL](https://huggingface.co/Qwen/Qwen3-VL-2B-Instruct) — Vision + language model
- [Kokoro TTS](https://github.com/thewh1teagle/kokoro-onnx) — Text-to-speech model
- [misaki G2P](https://huggingface.co/blog/hexgrad/g2p) — Grapheme-to-phoneme (future)
