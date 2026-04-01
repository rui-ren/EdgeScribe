# EDGESCRIBE REST API Reference

Base URL: `http://127.0.0.1:8080`

Start server: `edgescribe serve --port 8080`

All responses are JSON unless otherwise noted. CORS is enabled for local frontend development.

---

## Health & Info

### GET /v1/health

Check server status and loaded engines.

**Response:**
```json
{
  "status": "ok",
  "version": "0.1.0",
  "engines": {
    "asr": true,
    "llm": true,
    "vision": true,
    "tts": true
  }
}
```

### GET /v1/models

List loaded models.

**Response:**
```json
{
  "models": [
    {"name": "nemotron", "type": "asr", "loaded": true},
    {"name": "qwen3-vl", "type": "llm", "loaded": true},
    {"name": "qwen3-vl", "type": "vlm", "loaded": true},
    {"name": "kokoro", "type": "tts", "loaded": true}
  ]
}
```

---

## Speech-to-Text

### POST /v1/transcribe/file

Transcribe a WAV audio file.

**Request:** `multipart/form-data`
- `audio` — WAV file (16-bit PCM, any sample rate — will be resampled to 16kHz)

**Response:**
```json
{
  "text": "The patient presents with acute bronchitis.",
  "duration": 12.3
}
```

**Example (curl):**
```bash
curl -X POST http://localhost:8080/v1/transcribe/file \
  -F "audio=@meeting.wav"
```

**Example (JavaScript):**
```javascript
const form = new FormData();
form.append('audio', audioBlob, 'recording.wav');
const res = await fetch('http://localhost:8080/v1/transcribe/file', {
  method: 'POST',
  body: form
});
const { text, duration } = await res.json();
```

### POST /v1/transcribe/stream

Start a new streaming ASR session. Resets the transcriber state.

**Response:**
```json
{
  "status": "streaming_ready",
  "message": "POST audio chunks to /v1/transcribe/push as raw PCM (16kHz, float32, mono)"
}
```

### POST /v1/transcribe/push

Push a chunk of raw PCM audio. Call repeatedly as audio is captured.

**Request:** Raw binary body — float32 PCM samples, 16kHz, mono.

**Response:**
```json
{
  "text": "acute",
  "is_final": false,
  "transcript": "The patient presents with acute"
}
```

**Example (JavaScript):**
```javascript
// From Web Audio API or MediaRecorder
const pcmFloat32 = new Float32Array(audioBuffer);
const res = await fetch('http://localhost:8080/v1/transcribe/push', {
  method: 'POST',
  headers: { 'Content-Type': 'application/octet-stream' },
  body: pcmFloat32.buffer
});
const { text, transcript } = await res.json();
```

### POST /v1/transcribe/flush

Flush remaining audio and get the final complete transcript.

**Response:**
```json
{
  "text": "bronchitis.",
  "is_final": true,
  "transcript": "The patient presents with acute bronchitis."
}
```

### Streaming ASR Flow (Frontend Pattern)

```javascript
// 1. Start session
await fetch('/v1/transcribe/stream', { method: 'POST' });

// 2. Capture mic audio and push chunks
const stream = await navigator.mediaDevices.getUserMedia({ audio: true });
const context = new AudioContext({ sampleRate: 16000 });
const source = context.createMediaStreamSource(stream);
const processor = context.createScriptProcessor(4096, 1, 1);

processor.onaudioprocess = async (e) => {
  const pcm = e.inputBuffer.getChannelData(0);
  const res = await fetch('/v1/transcribe/push', {
    method: 'POST',
    body: pcm.buffer
  });
  const { text, transcript } = await res.json();
  document.getElementById('transcript').textContent = transcript;
};

source.connect(processor);
processor.connect(context.destination);

// 3. When done, flush
const final = await fetch('/v1/transcribe/flush', { method: 'POST' });
const { transcript } = await final.json();
```

---

## Chat / LLM

### POST /v1/chat

General chat completion.

**Request:**
```json
{
  "prompt": "What are the side effects of metformin?",
  "system": "You are a helpful medical assistant.",
  "max_length": 2048
}
```

**Response:**
```json
{
  "text": "Common side effects of metformin include..."
}
```

**Example (curl):**
```bash
curl -X POST http://localhost:8080/v1/chat \
  -H "Content-Type: application/json" \
  -d '{"prompt":"What is hypertension?","system":"You are a doctor."}'
```

### POST /v1/chat/soap

Generate SOAP notes from a transcript.

**Request:**
```json
{
  "transcript": "Doctor: How are you feeling today? Patient: I've had a cough for three days..."
}
```

**Response:**
```json
{
  "text": "S: Patient reports 3-day history of cough...\nO: ...\nA: ...\nP: ..."
}
```

### POST /v1/chat/summarize

Summarize text.

**Request:**
```json
{
  "text": "Long meeting transcript or document..."
}
```

**Response:**
```json
{
  "text": "Summary of the key points..."
}
```

---

## Vision

### POST /v1/vision/analyze

Analyze an image with a custom prompt.

**Request:** `multipart/form-data`
- `image` — image file (JPEG, PNG)
- `prompt` — text field with analysis prompt

**Response:**
```json
{
  "text": "The image shows a chest X-ray with..."
}
```

**Example (curl):**
```bash
curl -X POST http://localhost:8080/v1/vision/analyze \
  -F "image=@xray.jpg" \
  -F "prompt=Describe any abnormalities"
```

**Example (JavaScript):**
```javascript
const form = new FormData();
form.append('image', imageFile);
form.append('prompt', 'Extract all text from this document');
const res = await fetch('http://localhost:8080/v1/vision/analyze', {
  method: 'POST',
  body: form
});
const { text } = await res.json();
```

### POST /v1/vision/ocr

Extract text from an image (OCR).

**Request:** `multipart/form-data`
- `image` — image file

**Response:**
```json
{
  "text": "Rx: Amoxicillin 500mg\nSig: 1 cap PO TID x 10 days"
}
```

---

## Text-to-Speech

### POST /v1/tts/synthesize

Convert text to speech audio.

**Request:**
```json
{
  "text": "The patient presents with acute bronchitis.",
  "voice": "af_heart",
  "speed": 1.0
}
```

**Response:** Binary WAV audio (`Content-Type: audio/wav`)

**Example (JavaScript):**
```javascript
const res = await fetch('http://localhost:8080/v1/tts/synthesize', {
  method: 'POST',
  headers: { 'Content-Type': 'application/json' },
  body: JSON.stringify({ text: 'Hello world', voice: 'af_heart' })
});
const audioBlob = await res.blob();
const audio = new Audio(URL.createObjectURL(audioBlob));
audio.play();
```

### GET /v1/tts/voices

List available TTS voices.

**Response:**
```json
{
  "voices": ["af_heart", "default"]
}
```

---

## Error Responses

All errors return JSON:

```json
{
  "error": "Description of the error"
}
```

| Status | Meaning |
|--------|---------|
| 400 | Bad request (missing field, empty body) |
| 500 | Internal error (model failure) |
| 503 | Engine not loaded (model not available) |
