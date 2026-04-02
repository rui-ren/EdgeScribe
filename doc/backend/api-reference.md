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

General chat completion. Waits for the full response before returning.

Supports two modes:

**Single-turn** (prompt + system):
```json
{
  "prompt": "What are the side effects of metformin?",
  "system": "You are a helpful medical assistant.",
  "max_length": 2048
}
```

**Multi-turn** (messages array):
```json
{
  "messages": [
    {"role": "system", "content": "You are a helpful assistant."},
    {"role": "user", "content": "What is hypertension?"},
    {"role": "assistant", "content": "Hypertension is high blood pressure..."},
    {"role": "user", "content": "What are the treatment options?"}
  ],
  "max_length": 2048
}
```

**Response:**
```json
{
  "text": "Common side effects of metformin include..."
}
```

### POST /v1/chat/stream

Streaming chat completion using Server-Sent Events (SSE). Tokens are sent
as they are generated, enabling real-time display in the UI.

Accepts the same request body as `/v1/chat` (both single-turn and multi-turn).

**Response:** `Content-Type: text/event-stream` (chunked transfer encoding)

Each token is sent as an SSE event:
```
data: {"token":"Common"}

data: {"token":" side"}

data: {"token":" effects"}
```

When generation is complete, a final event is sent with the full text:
```
data: {"done":true,"text":"Common side effects of metformin include..."}
```

If an error occurs during generation:
```
data: {"error":"Model inference failed"}
```

**Example (JavaScript — streaming):**
```javascript
const res = await fetch('http://localhost:8080/v1/chat/stream', {
  method: 'POST',
  headers: { 'Content-Type': 'application/json' },
  body: JSON.stringify({
    messages: [
      { role: 'system', content: 'You are a helpful assistant.' },
      { role: 'user', content: 'What is hypertension?' }
    ]
  })
});

const reader = res.body.getReader();
const decoder = new TextDecoder();
let buffer = '';

while (true) {
  const { done, value } = await reader.read();
  if (done) break;

  buffer += decoder.decode(value, { stream: true });
  const lines = buffer.split('\n');
  buffer = lines.pop();

  for (const line of lines) {
    if (!line.startsWith('data: ')) continue;
    const data = JSON.parse(line.slice(6));
    if (data.done) {
      console.log('Full response:', data.text);
    } else if (data.token) {
      process.stdout.write(data.token);  // display token-by-token
    }
  }
}
```

**Example (curl):**
```bash
curl -N -X POST http://localhost:8080/v1/chat/stream \
  -H "Content-Type: application/json" \
  -d '{"prompt":"What is hypertension?","system":"You are a doctor."}'
```

**Notes:**
- The streaming endpoint auto-saves conversations to the memory database.
- Use `/v1/chat` (non-streaming) for programmatic access where you just need the final result.
- Use `/v1/chat/stream` for interactive UIs where you want real-time token display.
- The frontend automatically falls back to `/v1/chat` if streaming is unavailable.

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

---

## Memory / Sessions

Chat conversations and transcription sessions are automatically saved to a
local SQLite database (`~/.EDGESCRIBE/edgescribe.db`). These endpoints let the
frontend browse, search, and manage saved sessions.

### GET /v1/memory/sessions

List recent sessions (up to 50, newest first).

**Response:**
```json
{
  "sessions": [
    {
      "id": "s_a3f8b2",
      "type": "chat",
      "model": "qwen3-vl",
      "started_at": "2026-03-30T14:22:01",
      "message_count": 15
    }
  ]
}
```

### GET /v1/memory/sessions/:id

Get all messages and notes for a specific session.

**Response:**
```json
{
  "session_id": "s_a3f8b2",
  "messages": [
    {"role": "user", "content": "What is hypertension?", "created_at": "2026-03-30T14:22:05"},
    {"role": "assistant", "content": "Hypertension is...", "created_at": "2026-03-30T14:22:12"}
  ],
  "notes": [
    {"type": "soap", "output_text": "S: Patient reports...", "created_at": "2026-03-30T14:25:00"}
  ]
}
```

### POST /v1/memory/search

Full-text search across all saved messages.

**Request:**
```json
{
  "query": "metformin"
}
```

**Response:**
```json
{
  "results": [
    {
      "session_id": "s_a3f8b2",
      "role": "assistant",
      "content": "Metformin is a first-line medication...",
      "created_at": "2026-03-30T14:22:12"
    }
  ]
}
```

### DELETE /v1/memory/sessions/:id

Delete a session and all its messages and notes (cascade delete).

**Response:**
```json
{
  "deleted": "s_a3f8b2"
}
