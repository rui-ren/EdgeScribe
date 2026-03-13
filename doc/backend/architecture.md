# EDGESCRIBE Backend Architecture

## Overview

The EDGESCRIBE backend is a single C++ binary (`EDGESCRIBE.exe`) that provides:
1. A CLI interface for all AI operations
2. A local REST API server (`EDGESCRIBE serve`)

All inference runs on-device via ONNX Runtime and onnxruntime-genai. No cloud calls.

---

## Source Layout

```
src/
├── cli/main.cpp                  # CLI entry point — command routing, arg parsing
├── core/
│   ├── model_manager.h           # Model download, cache, manifest
│   └── model_manager.cpp         # HuggingFace download via curl/PowerShell
├── asr/                          # Speech-to-Text engine
│   ├── transcriber.h/cpp         # StreamingProcessor + Generator pipeline
│   ├── audio_capture.h/cpp       # Live mic capture via miniaudio
│   └── audio_file.h/cpp          # WAV file loader via miniaudio decoder
├── llm/                          # Language Model engine
│   └── llm_engine.h/cpp          # Chat, SOAP notes, summarize, fix terms
├── vision/                       # Vision + OCR engine
│   └── vision_engine.h/cpp       # MultiModalProcessor for image analysis
├── tts/                          # Text-to-Speech engine
│   ├── tts_engine.h/cpp          # Kokoro ONNX model via ORT C++ API
│   ├── phonemizer.h/cpp          # espeak-ng G2P (dynamically loaded)
│   └── kokoro_vocab.h            # IPA → Kokoro token ID mapping
└── server/                       # REST API server
    ├── api_server.h              # Server config and public API
    └── api_server.cpp            # HTTP routes via cpp-httplib
```

---

## Engine Details

### ASR Engine (`src/asr/`)

**Purpose**: Real-time speech-to-text using NVIDIA Parakeet TDT (Nemotron).

**Pipeline**:
```
Audio (16kHz float32 mono)
  → StreamingProcessor.Process(float[], count)
    → returns NamedTensors (mel spectrogram features)
  → Generator.SetInputs(NamedTensors)
  → Generator.GenerateNextToken()  [loop until done]
  → Generator.GetNextTokens()
  → TokenizerStream.Decode(token_id)
    → partial text output
```

**Key classes**:
- `Transcriber` — owns the full pipeline (model, processor, generator, tokenizer stream). Thread-safe via mutex.
- `AudioCapture` — wraps miniaudio for 16kHz mono float32 mic input. Fires callback per audio chunk.
- `AudioFile` — loads WAV/MP3/FLAC via miniaudio decoder, resamples to 16kHz mono.

**Constants**:
- Chunk size: 8960 samples (560ms at 16kHz)
- Silence padding: 4 chunks for right context
- Model sample rate: 16kHz

**onnxruntime-genai API used**:
- `OgaModel::Create(path)`
- `OgaStreamingProcessor::Create(model)` / `.Process()` / `.Flush()`
- `OgaGenerator::Create(model, params)` / `.SetInputs()` / `.GenerateNextToken()` / `.GetNextTokens()`
- `OgaTokenizerStream::Create(tokenizer)` / `.Decode(token)`

---

### LLM Engine (`src/llm/`)

**Purpose**: Text generation for chat, SOAP notes, summarization, terminology fixes.

**Pipeline**:
```
User message
  → FormatChatMessages() — builds JSON, applies chat template
  → Tokenizer.Encode(prompt) → OgaSequences
  → Generator.AppendTokenSequences()
  → Generator.GenerateNextToken()  [loop]
  → TokenizerStream.Decode(token)
    → streaming text output
```

**Pre-built prompts**:
- `GenerateSOAPNotes(transcript)` — medical scribe system prompt
- `Summarize(text)` — concise summary prompt
- `FixMedicalTerms(transcript)` — correction-only prompt

**onnxruntime-genai API used**:
- `OgaModel::Create(path)`
- `OgaTokenizer::Create(model)` / `.Encode()` / `.ApplyChatTemplate()`
- `OgaSequences::Create()`
- `OgaGenerator::Create()` / `.AppendTokenSequences()` / `.GenerateNextToken()` / `.GetNextTokens()`
- `OgaGeneratorParams::Create()` / `.SetSearchOption("max_length", n)`

---

### Vision Engine (`src/vision/`)

**Purpose**: Image understanding, OCR, medical image analysis using Qwen3-VL.

**Pipeline**:
```
Image file + text prompt
  → FormatVisionPrompt() — adds model-specific image tags
  → OgaImages::Load(paths)
  → MultiModalProcessor.ProcessImages(prompt, images) → NamedTensors
  → Generator.SetInputs(NamedTensors)
  → Generator.GenerateNextToken()  [loop]
  → TokenizerStream.Decode(token)
    → text output
```

**Model-specific formatting** (auto-detected via `model_->GetType()`):
- Qwen-VL: `<|vision_start|><|image_pad|><|vision_end|>`
- Phi-3V: `<|image_1|>\n`
- Phi-4: `<|image_1|>\n`

**onnxruntime-genai API used**:
- `OgaMultiModalProcessor::Create(model)` / `.ProcessImages(prompt, images)`
- `OgaImages::Load(paths)`
- Same Generator/Tokenizer as LLM

---

### TTS Engine (`src/tts/`)

**Purpose**: Text-to-speech using Kokoro ONNX model.

**Pipeline**:
```
Text
  → Phonemizer.TextToPhonemes(text)   [espeak-ng G2P or fallback]
  → PhonemeStringToTokens(phonemes)   [IPA → Kokoro token IDs]
  → ONNX Runtime Session.Run(tokens, style, speed)
  → float[] PCM audio (24kHz)
  → miniaudio playback or WAV file
```

**Key components**:
- `TtsEngine` — manages ONNX Runtime session, voice embeddings, synthesis
- `Phonemizer` — dynamically loads espeak-ng at runtime via dlopen/LoadLibrary
- `kokoro_vocab.h` — maps ~95 IPA symbols to Kokoro token IDs

**ONNX Runtime C++ API used** (not GenAI — plain ORT):
- `Ort::Env`, `Ort::SessionOptions`, `Ort::Session`
- `Ort::Value::CreateTensor<T>()`
- `session->Run(options, input_names, inputs, output_names, outputs)`

**Audio output**:
- `Speak()` — plays through speakers via miniaudio playback device
- `SynthesizeToFile()` — writes WAV (16-bit PCM header + data)
- `Synthesize()` — returns float32 PCM buffer

---

### Model Manager (`src/core/`)

**Purpose**: Download models from HuggingFace, manage local cache.

**Cache locations**:
- Windows: `%LOCALAPPDATA%\EDGESCRIBE\models\`
- macOS/Linux: `~/.EDGESCRIBE/models/`
- Override: `EDGESCRIBE_MODEL_DIR` env var

**Model manifest** (built-in, hardcoded):

| Name | Type | Config File | HuggingFace Repo | Files |
|------|------|-------------|-------------------|-------|
| `nemotron` | asr | `genai_config.json` | `EDGESCRIBE/nemotron-onnx-cpu` | encoder.onnx, decoder.onnx, joint.onnx, configs, tokenizer |
| `qwen3-vl` | vlm | `genai_config.json` | `EDGESCRIBE/qwen3-vl-2b-onnx-cpu-int4` | model.onnx, model.onnx.data, configs, tokenizer |
| `kokoro` | tts | `model.onnx` | `EDGESCRIBE/kokoro-onnx` | model.onnx, voices.bin, config.json |

**Download method**: Shells out to `curl` (macOS/Linux) or `Invoke-WebRequest` (Windows). URL-validated to HuggingFace only.

**Path resolution**: Accepts model name (`nemotron`) or direct path (`/path/to/model`). Direct paths checked for `genai_config.json`, `config.json`, or `model.onnx`.

---

## REST API Server (`src/server/`)

**Library**: [cpp-httplib](https://github.com/yhirose/cpp-httplib) v0.37 (single-header, MIT)

**Binding**: `127.0.0.1` only (localhost, never exposed to network)

**CORS**: Enabled for all origins (for local frontend development)

### Endpoints

#### Health & Info

| Method | Path | Description |
|--------|------|-------------|
| GET | `/v1/health` | `{"status":"ok","version":"0.1.0","engines":{...}}` |
| GET | `/v1/models` | List loaded model names and types |

#### Speech-to-Text

| Method | Path | Body | Response |
|--------|------|------|----------|
| POST | `/v1/transcribe/file` | Multipart form: `audio` file | `{"text":"...","duration":12.3}` |
| POST | `/v1/transcribe/stream` | `{}` | `{"status":"streaming_ready"}` — resets ASR session |
| POST | `/v1/transcribe/push` | Raw PCM float32 bytes | `{"text":"partial","is_final":false,"transcript":"full so far"}` |
| POST | `/v1/transcribe/flush` | (empty) | `{"text":"final","is_final":true,"transcript":"complete"}` |

**Streaming ASR flow**:
1. `POST /v1/transcribe/stream` — reset session
2. `POST /v1/transcribe/push` (repeat) — send audio chunks
3. `POST /v1/transcribe/flush` — get final result

#### Chat / LLM

| Method | Path | Body | Response |
|--------|------|------|----------|
| POST | `/v1/chat` | `{"prompt":"...","system":"...","max_length":2048}` | `{"text":"..."}` |
| POST | `/v1/chat/soap` | `{"transcript":"..."}` | `{"text":"S: ... O: ... A: ... P: ..."}` |
| POST | `/v1/chat/summarize` | `{"text":"..."}` | `{"text":"..."}` |

#### Vision

| Method | Path | Body | Response |
|--------|------|------|----------|
| POST | `/v1/vision/analyze` | Multipart: `image` file + `prompt` field | `{"text":"..."}` |
| POST | `/v1/vision/ocr` | Multipart: `image` file | `{"text":"..."}` |

#### Text-to-Speech

| Method | Path | Body | Response |
|--------|------|------|----------|
| POST | `/v1/tts/synthesize` | `{"text":"...","voice":"af_heart","speed":1.0}` | Binary WAV audio |
| GET | `/v1/tts/voices` | — | `{"voices":["af_heart",...]}` |

---

## Build System

**CMake 3.18+**, C++20 required.

**Key CMake variables**:
- `ORT_GENAI_PATH` — path to onnxruntime-genai (headers + libs)
- Auto-detects: `src/ort_genai.h`, `build/Windows/Release/Release/*.lib`

**Dependencies** (all vendored or auto-found):
- `onnxruntime-genai` — linked via import library
- `onnxruntime` — linked for TTS engine
- `miniaudio.h` — single-header, in `include/`
- `httplib.h` — single-header, in `include/`
- `espeak-ng` — optional, dynamically loaded at runtime

**Platform-specific**:
- Windows: links `ws2_32`, `crypt32`, `winmm`
- macOS: links CoreAudio, AudioToolbox, AudioUnit, CoreFoundation
- Linux: links pthreads, dl, m, pulse (optional)

**Build command**:
```bash
cmake -B build -DORT_GENAI_PATH=/path/to/onnxruntime-genai
cmake --build build --config Release
```

---

## Threading Model

- Each engine has its own `std::mutex` — only one request per engine at a time
- The API server is single-threaded (cpp-httplib default) but handles concurrent requests via the mutexes
- ASR live capture runs the mic callback on miniaudio's audio thread
- The `Transcriber` is thread-safe (all methods lock the internal mutex)

---

## Error Handling

- GenAI errors propagate via `OgaCheckResult()` → `std::runtime_error`
- API endpoints catch all exceptions and return `{"error":"message"}` with appropriate HTTP status codes
- Model not loaded → 503 Service Unavailable
- Bad request → 400 Bad Request
- Internal error → 500 Internal Server Error
