# EDGESCRIBE Frontend — Design & Plan

## Approach: Embedded Web UI (served by EDGESCRIBE itself)

The frontend is a single-page web app served by the same `EDGESCRIBE serve` binary. No separate frontend server needed. Users run `EDGESCRIBE serve` and open `http://localhost:8080` in their browser.

```
EDGESCRIBE serve --port 8080
  ├── API:  /v1/*          → REST endpoints (already implemented)
  └── UI:   /              → static HTML/JS/CSS (embedded in binary or served from www/ folder)
```

---

## Tech Stack

| Layer | Choice | Why |
|-------|--------|-----|
| **Framework** | Vanilla HTML + CSS + JavaScript (no build step) | Zero dependencies, served as static files, no npm/webpack needed |
| **CSS** | Tailwind CSS (CDN) or custom minimal CSS | Clean, responsive, dark mode |
| **Audio** | Web Audio API + MediaRecorder | Browser-native mic access |
| **HTTP** | Fetch API | Native, no axios needed |
| **Icons** | Lucide (CDN) | Clean, MIT-licensed |
| **Alternative** | React/Vue via CDN if complexity grows | Can upgrade later without build system |

**Why no React/Vue/build system?**
- The UI is simple: a few panels and buttons
- Static files can be embedded in the C++ binary or served from disk
- Zero frontend build step = easier for contributors
- Can upgrade to React later if needed

---

## Pages / Views

### 1. Dashboard (Home)

```
┌──────────────────────────────────────────────────────────────┐
│  EDGESCRIBE                               [⚙ Settings]      │
├──────────────────────────────────────────────────────────────┤
│                                                              │
│  ┌─────────┐  ┌─────────┐  ┌─────────┐  ┌─────────┐       │
│  │ 🎤 ASR  │  │ 🧠 Chat │  │ 🖼 Vision│  │ 🔊 TTS  │       │
│  │         │  │         │  │         │  │         │       │
│  │ Ready ✅│  │ Ready ✅│  │ Ready ✅│  │ Ready ✅│       │
│  └─────────┘  └─────────┘  └─────────┘  └─────────┘       │
│                                                              │
│  Recent Activity:                                            │
│  • 2 min ago: Transcribed meeting.wav (5.2 min)             │
│  • 10 min ago: Generated SOAP notes                         │
│                                                              │
└──────────────────────────────────────────────────────────────┘
```

### 2. Live Transcription

```
┌──────────────────────────────────────────────────────────────┐
│  🎤 Live Transcription           [● Recording] [⏹ Stop]     │
├──────────────────────────────────────────────────────────────┤
│                                                              │
│  ┌────────────────────────────────────────────────────────┐  │
│  │ The patient presents with a three-day history of      │  │
│  │ persistent cough and low-grade fever. She reports      │  │
│  │ no shortness of breath or chest pain. ▊                │  │
│  │                                                        │  │
│  │ [text appears in real-time as you speak]               │  │
│  └────────────────────────────────────────────────────────┘  │
│                                                              │
│  Duration: 0:45  │  Chunks: 82  │  Model: nemotron          │
│                                                              │
│  [📋 Copy] [💾 Save as .txt] [📝 Generate SOAP Notes]       │
│                                                              │
└──────────────────────────────────────────────────────────────┘
```

### 3. File Transcription

```
┌──────────────────────────────────────────────────────────────┐
│  📁 File Transcription                                       │
├──────────────────────────────────────────────────────────────┤
│                                                              │
│  ┌────────────────────────────────────────────────────────┐  │
│  │                                                        │  │
│  │         Drop audio file here or click to browse        │  │
│  │                    (.wav, .mp3)                         │  │
│  │                                                        │  │
│  └────────────────────────────────────────────────────────┘  │
│                                                              │
│  ████████████████████████░░░░░░░  75%  Processing...         │
│                                                              │
│  ┌────────────────────────────────────────────────────────┐  │
│  │ Transcript result appears here...                      │  │
│  └────────────────────────────────────────────────────────┘  │
│                                                              │
│  [📋 Copy] [💾 Save] [📝 SOAP] [📊 Summarize]              │
│                                                              │
└──────────────────────────────────────────────────────────────┘
```

### 4. Chat

```
┌──────────────────────────────────────────────────────────────┐
│  🧠 Chat                                                     │
├──────────────────────────────────────────────────────────────┤
│                                                              │
│  ┌────────────────────────────────────────────────────────┐  │
│  │ 🤖 How can I help you today?                          │  │
│  │                                                        │  │
│  │ 👤 What medications interact with metformin?           │  │
│  │                                                        │  │
│  │ 🤖 Metformin can interact with several medications:   │  │
│  │    • Contrast dyes (risk of lactic acidosis)          │  │
│  │    • Carbonic anhydrase inhibitors                     │  │
│  │    • ...                                               │  │
│  └────────────────────────────────────────────────────────┘  │
│                                                              │
│  ┌──────────────────────────────────────────┐ [Send]         │
│  │ Type your message...                     │                │
│  └──────────────────────────────────────────┘                │
│                                                              │
└──────────────────────────────────────────────────────────────┘
```

### 5. Vision / OCR

```
┌──────────────────────────────────────────────────────────────┐
│  🖼 Vision & OCR                                             │
├──────────────────────────────────────────────────────────────┤
│                                                              │
│  ┌──────────────────┐  ┌──────────────────────────────────┐ │
│  │                  │  │ Extracted text / Analysis:        │ │
│  │   [Image         │  │                                   │ │
│  │    Preview]      │  │ Rx: Amoxicillin 500mg            │ │
│  │                  │  │ Sig: 1 cap PO TID x 10 days     │ │
│  │                  │  │ Disp: 30 capsules                │ │
│  └──────────────────┘  │ Refills: 2                       │ │
│                         └──────────────────────────────────┘ │
│  [📂 Upload Image]  [🔍 OCR]  [💬 Analyze with prompt]     │
│                                                              │
│  Prompt: ┌──────────────────────────────────────┐           │
│          │ Extract medications and dosages      │           │
│          └──────────────────────────────────────┘           │
│                                                              │
└──────────────────────────────────────────────────────────────┘
```

### 6. Text-to-Speech

```
┌──────────────────────────────────────────────────────────────┐
│  🔊 Text-to-Speech                                           │
├──────────────────────────────────────────────────────────────┤
│                                                              │
│  ┌────────────────────────────────────────────────────────┐  │
│  │ Enter text to speak:                                   │  │
│  │                                                        │  │
│  │ The patient presents with acute bronchitis.            │  │
│  │                                                        │  │
│  └────────────────────────────────────────────────────────┘  │
│                                                              │
│  Voice: [af_heart ▼]    Speed: [1.0]                        │
│                                                              │
│  [▶ Play]  [💾 Save WAV]                                    │
│                                                              │
│  🔊 ███████████████████░░░░░░░  Playing...                   │
│                                                              │
└──────────────────────────────────────────────────────────────┘
```

### 7. Settings

```
┌──────────────────────────────────────────────────────────────┐
│  ⚙ Settings                                                  │
├──────────────────────────────────────────────────────────────┤
│                                                              │
│  Models:                                                     │
│  ┌──────────────────────────────────────────────────────┐   │
│  │ nemotron    ASR    670 MB   ✅ Downloaded            │   │
│  │ qwen3-vl    VLM    1.5 GB  ✅ Downloaded            │   │
│  │ kokoro      TTS    300 MB  ⬇ Download               │   │
│  └──────────────────────────────────────────────────────┘   │
│                                                              │
│  Audio Input: [Default Microphone ▼]                        │
│  Theme: [Dark ▼]                                            │
│  API Port: [8080]                                            │
│                                                              │
└──────────────────────────────────────────────────────────────┘
```

---

## File Structure

```
www/
├── index.html              # Single HTML file — all views
├── css/
│   └── style.css           # Custom styles + dark mode
├── js/
│   ├── app.js              # Router, state management
│   ├── api.js              # API client (fetch wrappers)
│   ├── audio.js            # Web Audio API mic capture
│   ├── transcribe.js       # Live transcription UI logic
│   ├── chat.js             # Chat interface logic
│   ├── vision.js           # Vision/OCR upload + display
│   └── tts.js              # TTS playback logic
└── assets/
    └── logo.svg            # EDGESCRIBE logo
```

---

## Implementation Plan

### Phase 1: Core UI (MVP)

| Task | Priority | Effort |
|------|----------|--------|
| Static HTML shell with navigation | P0 | 2h |
| Dashboard with engine status (`GET /v1/health`) | P0 | 1h |
| Live transcription page (mic → push → display) | P0 | 4h |
| File upload transcription page | P0 | 2h |
| Copy/Save transcript buttons | P0 | 1h |
| Dark mode CSS | P1 | 1h |
| **Total Phase 1** | | **~11h** |

### Phase 2: Full Features

| Task | Priority | Effort |
|------|----------|--------|
| Chat interface | P1 | 3h |
| Vision/OCR upload + display | P1 | 3h |
| TTS playback in browser | P1 | 2h |
| SOAP notes generation (from transcript) | P1 | 2h |
| Settings page (model status) | P2 | 2h |
| **Total Phase 2** | | **~12h** |

### Phase 3: Polish

| Task | Priority | Effort |
|------|----------|--------|
| Responsive mobile layout | P2 | 2h |
| Keyboard shortcuts | P2 | 1h |
| Export (TXT, SRT, PDF) | P2 | 3h |
| Transcription history (localStorage) | P2 | 2h |
| Audio waveform visualization | P3 | 3h |

---

## How to Serve the UI

Two options:

### Option A: Serve from `www/` directory (development)

The API server serves static files from `www/` alongside the API:

```cpp
// In api_server.cpp — add static file serving
svr.set_mount_point("/", "./www");
```

### Option B: Embed in binary (production)

Use CMake to embed the HTML/CSS/JS as binary resources. The server returns them from memory — no filesystem needed. This makes the single `EDGESCRIBE.exe` fully self-contained.

**Recommendation**: Start with Option A (easier to develop), migrate to Option B for release.

---

## Key Frontend Patterns

### API Client

```javascript
// js/api.js
const API_BASE = 'http://localhost:8080';

export async function transcribeFile(file) {
  const form = new FormData();
  form.append('audio', file);
  const res = await fetch(`${API_BASE}/v1/transcribe/file`, {
    method: 'POST', body: form
  });
  return res.json();
}

export async function chat(prompt, system = '') {
  const res = await fetch(`${API_BASE}/v1/chat`, {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({ prompt, system })
  });
  return res.json();
}

export async function synthesize(text, voice = 'af_heart') {
  const res = await fetch(`${API_BASE}/v1/tts/synthesize`, {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({ text, voice })
  });
  return res.blob(); // WAV audio
}
```

### Live Mic Capture

```javascript
// js/audio.js
export class MicCapture {
  constructor(onChunk) {
    this.onChunk = onChunk;
  }

  async start() {
    this.stream = await navigator.mediaDevices.getUserMedia({
      audio: { sampleRate: 16000, channelCount: 1 }
    });
    this.context = new AudioContext({ sampleRate: 16000 });
    const source = this.context.createMediaStreamSource(this.stream);
    
    this.processor = this.context.createScriptProcessor(4096, 1, 1);
    this.processor.onaudioprocess = (e) => {
      const pcm = e.inputBuffer.getChannelData(0);
      this.onChunk(pcm);
    };
    
    source.connect(this.processor);
    this.processor.connect(this.context.destination);
  }

  stop() {
    this.processor?.disconnect();
    this.stream?.getTracks().forEach(t => t.stop());
    this.context?.close();
  }
}
```

---

## Next Steps

1. Create the `www/` directory with `index.html`, CSS, and JS files
2. Add static file serving to `api_server.cpp`
3. Implement Live Transcription page first (highest value)
4. Add remaining pages iteratively
