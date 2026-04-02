# EDGESCRIBE Frontend

The EDGESCRIBE frontend is a single-page web application served by the same
C++ binary that runs the AI engines. No separate frontend server, no build
tools, no npm — just static HTML, CSS, and JavaScript.

---

## How It Runs

There are two ways to use the UI:

| Mode | Command | How it works |
|------|---------|--------------|
| **Native app** (recommended) | `edgescribe gui` | Opens a native desktop window via [webview](https://github.com/webview/webview). No browser needed. |
| **Browser** | `edgescribe serve` | Starts the HTTP server; user opens `http://localhost:8080` in any browser. |

Both modes serve the same `www/` files and talk to the same REST API.

See [`doc/webview-integration.md`](../webview-integration.md) for full details
on the native GUI.

---

## Tech Stack

| Layer | Choice | Why |
|-------|--------|-----|
| **Framework** | Vanilla HTML + CSS + JS (ES6 modules) | Zero dependencies, no build step, easy to contribute |
| **Styling** | Custom CSS with CSS custom properties | Dark theme, fully self-contained, no CDN needed |
| **Audio** | Web Audio API (`AudioContext` + `ScriptProcessor`) | Browser-native mic capture at 16 kHz |
| **HTTP** | `fetch()` API | Native, no libraries needed |
| **Icons** | Emoji | Universal, zero-dependency, works offline |
| **Notifications** | Custom toast system | Non-blocking, replaces intrusive `alert()` dialogs |

**Why no React/Vue/build system?**

EDGESCRIBE is designed to work fully offline with zero internet dependencies.
A vanilla JS approach means:
- No npm, no webpack, no node_modules
- Static files work immediately — drop into `www/` and go
- Easy for anyone to modify (just edit HTML/CSS/JS)
- Can be embedded in the C++ binary for a single-file distribution

---

## File Structure

```
www/
├── index.html          # Single-page app shell — all pages in one file
├── css/
│   └── style.css       # Complete design system (colors, layout, components)
└── js/
    ├── app.js          # App controller — routing, page setup, event handlers
    ├── api.js          # API client — thin fetch() wrappers for /v1/ endpoints
    └── lib/
        └── marked.min.js   # Markdown parser (downloaded at build time)
```

All frontend logic lives in two JS files:
- **`api.js`** handles HTTP calls (what to send)
- **`app.js`** handles UI behavior (what to show)
- **`marked.min.js`** is a third-party library for rendering Markdown in chat/vision results

---

## Pages

### 🏠 Home (Dashboard)

The landing page shown on launch.

- **Welcome hero** — greeting with a description of what the app does
- **Privacy pill** — prominent badge: "🛡️ 100% Private — Nothing leaves your device"
- **Status cards** — clickable tiles for each AI engine, showing ready/not-available
- **Quick actions** — large buttons to jump to the most common tasks (Record, Upload, Chat, Image, Medical Notes)
- **Recent Sessions** — list of past sessions pulled from backend SQLite; click to copy
- **Onboarding wizard** — first-launch overlay guiding new users through model setup

### 🎤 Record & Transcribe

Real-time speech-to-text from the user's microphone.

- **Transcript area** — text appears in real time with a blinking cursor (top)
- **Action bar** — Copy text, Save as file, Generate SOAP notes, Clear
- **Record hero** (bottom) — large red circle button with status text and hints
- **Audio waveform** — real-time waveform visualization during recording
- **Recording stats** — elapsed time and chunk count
- When recording stops, transcript is auto-saved to backend SQLite

Audio flows as: `Mic → Web Audio API (16 kHz PCM) → POST /v1/transcribe/push → display`

### 📁 Upload Audio

Transcribe a pre-recorded audio file.

- **Drop zone** — drag-and-drop area with click-to-browse fallback
- **Progress bar** — gradient-filled bar during processing
- **Transcript area** — result displayed with copy/save buttons
- Supports WAV, MP3, and FLAC
- Transcript auto-saved to backend SQLite

### 💬 Chat

Conversational AI powered by the on-device LLM with streaming responses.

- **Session sidebar** (left panel) — past conversations from SQLite, searchable,
  click to load, delete button per session, "New Chat" button
- **Message thread** — scrolling list of user/assistant messages with avatars
- **Streaming responses** — tokens appear one-by-one via SSE (`/v1/chat/stream`)
- **Markdown rendering** — assistant responses rendered with headers, bold, lists,
  code blocks, tables via `marked.js`
- **Per-message actions** — hover to reveal 📋 Copy and 🔊 Read Aloud buttons
- **Drag-and-drop images** — drop an image into chat to ask questions about it
  (routes to Vision API, shows inline preview)
- **System prompt editor** — collapsible panel to customize AI behavior
- **Chat export** — Word document (.doc) or PDF (via print dialog)
- **Context indicator** — estimated token usage bar at the bottom
- **Auto-grow textarea** — input grows as you type, Enter to send, Shift+Enter for newline
- **Keyboard shortcuts** — Ctrl+Shift+R (record), Ctrl+Shift+N (new chat), Ctrl+Shift+C (copy last response)

### 🖼️ Image Analysis

Upload a photo for AI description or text extraction (OCR).

- **Two-column layout** — image + prompt on the left, result on the right
- **Two actions** — "Analyze Image" (general description) and "Extract Text (OCR)"
- **Custom prompt** — user can ask specific questions about the image
- **Markdown rendering** — results rendered as formatted Markdown
- **Result actions** — Copy, Read Aloud, and "Continue in Chat" (injects result
  into chat history and navigates to Chat page for follow-up questions)

### 📋 Medical Notes

Upload or paste clinical documents to extract structured medical information.

- **Document input** — drag-and-drop file upload (.txt, .csv, .md) or paste text directly
- **Analysis types** (radio cards):
  - 📊 **Summary** — key findings, diagnoses, medications, procedures, recommendations
  - 💊 **Medications** — all medications with dosage, frequency, purpose
  - 🩺 **Diagnoses & Procedures** — conditions, treatments, lab results, referrals
  - 📝 **SOAP Notes** — reformat into Subjective/Objective/Assessment/Plan
- **Chunked processing** — large documents (>8K chars) are automatically split into
  chunks, analyzed independently, then merged (map-reduce pattern)
- **Streaming results** — tokens appear in real-time during analysis
- **Result actions** — Copy, export as Word document, Read Aloud via TTS

### 🔊 Read Aloud

Convert text to natural-sounding speech.

- **Text input** — large textarea for typing or pasting
- **Voice selector** — dropdown with 8 Kokoro voices (male/female, American/British)
- **Speed slider** — adjustable from 0.5× to 2.0×
- **Play / Save** — hear it immediately or download as a WAV file
- **Audio player** — native HTML5 audio element for playback control

### ⚙️ Settings

Model management and system information.

- **Model table** — shows each AI model, what it powers, its size, and status
- **About section** — version, license, GitHub link, privacy statement

---

## Design System

### Color Palette

The UI uses a warm dark theme with an indigo accent. All colors are defined
as CSS custom properties in `:root` for easy theming.

| Token | Value | Usage |
|-------|-------|-------|
| `--bg-body` | `#0d1117` | Page background |
| `--bg-secondary` | `#161b22` | Cards, sidebar |
| `--bg-tertiary` | `#1c2333` | Inputs, elevated surfaces |
| `--text-primary` | `#e6edf3` | Body text |
| `--text-secondary` | `#8b949e` | Descriptions, labels |
| `--accent` | `#6366f1` | Buttons, links, active states (indigo) |
| `--success` | `#34d399` | Ready indicators, privacy badges (emerald) |
| `--danger` | `#ef4444` | Record button, errors (red) |

### Typography

System font stack (no web fonts, works offline):

```css
font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', Roboto,
             'Helvetica Neue', Arial, sans-serif;
```

Base size: `15px`, line height: `1.6`.

### Key Components

| Component | Class | Description |
|-----------|-------|-------------|
| Card | `.card` | Rounded container with subtle border |
| Status card | `.stat-card` | Dashboard tile with icon, label, status; lifts on hover |
| Button | `.btn`, `.btn-primary`, `.btn-lg`, `.btn-sm` | Rounded buttons; primary uses gradient accent |
| Record button | `.record-btn` | Red-bordered circle with pulse animation while recording |
| Record hero | `.record-hero` | Centered card containing record button, status, and waveform |
| Waveform canvas | `.waveform-canvas` | Real-time audio waveform during recording |
| Transcript area | `.transcript-area` | Scrollable text display with blinking cursor |
| Drop zone | `.drop-zone` | Dashed-border area for drag-and-drop file upload |
| Toast | `.toast` | Slide-in notification (success/error/info); auto-dismisses |
| Privacy badge | `.privacy-badge` | Green-tinted badge emphasizing on-device processing |
| Welcome hero | `.welcome-hero` | Gradient-background hero section on the dashboard |
| Session sidebar | `.session-sidebar` | Left panel in chat with session list, search, new chat button |
| Session item | `.session-item` | Clickable session entry with title, meta, and delete button |
| Message actions | `.msg-actions` | Copy and Read Aloud buttons on chat messages (hover to reveal) |
| Chat image preview | `.chat-image-preview` | Inline preview strip when dropping an image into chat |
| System prompt | `.system-prompt-section` | Collapsible editor above chat input |
| Context indicator | `.context-indicator` | Token usage bar in chat footer |
| Notes options | `.notes-analysis-options` | 2×2 grid of radio cards for analysis type selection |
| Markdown body | `.markdown-body` | Styled container for rendered Markdown (headers, code, tables) |
| Onboarding overlay | `.onboarding-overlay` | First-launch wizard with step dots |
| Theme toggle | `.theme-toggle` | Dark/light mode switch button in sidebar header |
| Select input | `.select-input` | Styled dropdown (used for TTS voice selector) |
| Range input | `.range-input` | Styled slider (used for TTS speed) |

### Animations

| Animation | Where | Effect |
|-----------|-------|--------|
| `pageIn` | Page transitions | Fade + slide up (0.3s) |
| `recordPulse` | Record button | Expanding red ring while recording |
| `blink` | Transcript cursor | Blinking caret in live transcript |
| `pulse` | Status dots | Fading pulse for "loading" engine status |
| `toastIn` / `toastOut` | Notifications | Slide up on appear, fade out on dismiss |
| Button hover | All buttons | Subtle lift (`translateY(-1px)`) |

### Responsive Breakpoints

| Breakpoint | Changes |
|------------|---------|
| `≤ 900px` | Vision layout collapses to single column |
| `≤ 768px` | Sidebar becomes a slide-out drawer with hamburger toggle; chat session sidebar hidden; reduced padding |
| `≤ 600px` | Medical Notes analysis options collapse to single column |
| `≤ 480px` | Card grid becomes single column; buttons stack vertically |

---

## JavaScript Architecture

### `api.js` — API Client

A thin wrapper around `fetch()`. Every backend endpoint has a matching
exported function. The base URL is `window.location.origin` (same host).

```
health()                → GET  /v1/health
listModels()            → GET  /v1/models
transcribeFile(file)    → POST /v1/transcribe/file     (multipart)
transcribeStreamStart() → POST /v1/transcribe/stream
transcribePush(buffer)  → POST /v1/transcribe/push     (raw PCM)
transcribeFlush()       → POST /v1/transcribe/flush
chat(prompt)            → POST /v1/chat                (JSON, full response)
chatMultiTurn(messages)  → POST /v1/chat                (JSON, multi-turn)
chatStream(msgs, onTok) → POST /v1/chat/stream         (SSE, token-by-token)
generateSoap(text)      → POST /v1/chat/soap           (JSON)
summarize(text)         → POST /v1/chat/summarize      (JSON)
analyzeImage(file, p)   → POST /v1/vision/analyze      (multipart)
ocrImage(file)          → POST /v1/vision/ocr           (multipart)
synthesize(text, v, s)  → POST /v1/tts/synthesize      (JSON → WAV blob)
listVoices()            → GET  /v1/tts/voices
getSessions()           → GET  /v1/memory/sessions
getSession(id)          → GET  /v1/memory/sessions/:id
searchMemory(query)     → POST /v1/memory/search       (JSON)
deleteSession(id)       → DELETE /v1/memory/sessions/:id
```

### `app.js` — App Controller

Handles all UI logic in a single module:

| Function | Purpose |
|----------|---------|
| `navigate(page)` | SPA router — shows/hides page divs, updates active nav item |
| `init()` | Entry point — sets up nav, health check, page handlers, mobile toggle |
| `showToast(msg, type)` | Creates a toast notification (success/error/info) |
| `refreshHealth()` | Calls `/v1/health` and updates engine status indicators |
| `setupLiveTranscription()` | Mic recording, PCM streaming, transcript display |
| `setupFileTranscription()` | Drag-and-drop upload, progress bar, result display |
| `setupChat()` | Message threading, streaming responses, session management |
| `setupVision()` | Image upload, analyze/OCR buttons, result display |
| `setupTTS()` | Text input, voice/speed selector, play/save audio |
| `setupHistory()` | Dashboard transcript history from backend SQLite |
| `setupOnboarding()` | First-launch welcome wizard |
| `setupKeyboardShortcuts()` | Global keyboard shortcuts |
| `loadSessionList()` | Fetch and render session sidebar from backend |
| `loadSession(id)` | Load a past conversation into the chat view |
| `startNewChat()` | Reset chat state for a new conversation |
| `exportChatAsWord()` | Export chat as .doc file (opens in Word) |
| `startWaveformDraw()` | Real-time audio waveform during recording |
| `updateContextIndicator()` | Estimated token usage bar in chat |

### Streaming Chat Architecture

The chat uses **Server-Sent Events (SSE)** for real-time token display:

```
User sends message
  → POST /v1/chat/stream (with message history)
  → Backend generates tokens one-by-one via llama.cpp
  → Each token sent as SSE event: data: {"token":"word"}\n\n
  → Frontend reads via fetch() ReadableStream
  → Each token appended to the message div in real-time
  → On completion: data: {"done":true,"text":"full response"}\n\n
  → Frontend re-renders full text as formatted Markdown
  → Falls back to POST /v1/chat if streaming unavailable
```

This gives users the experience of watching the AI "type" its response,
similar to ChatGPT or other modern AI interfaces.

### Routing

Navigation is hash-free — pages are `<div class="page">` elements toggled
by adding/removing the `.active` class. Each nav item has a `data-page`
attribute that maps to `page-{name}`.

### Error Handling

All API errors are caught and displayed as toast notifications instead of
`alert()` dialogs. Toasts slide in from the bottom-right and auto-dismiss
after 3 seconds.

---

## UX Design Decisions

The UI is designed to be usable by non-technical users (medical staff,
administrative personnel, everyday users). Key decisions:

### Friendly Language

Technical terms are replaced with plain language:

| Before (technical) | After (user-friendly) |
|---|---|
| "Live Transcription" | "Record & Transcribe" |
| "File Transcription" | "Upload Audio" |
| "Vision & OCR" | "Image Analysis" |
| "Text-to-Speech" | "Read Aloud" |
| "ASR / LLM / VLM / TTS" | "Speech / Chat / Vision / Voice" |
| "● Ready" / "○ Not loaded" | "✓ Ready to use" / "○ Not available" |

### Privacy-First Messaging

Privacy is EDGESCRIBE's core value, so it's surfaced prominently:

- **Sidebar footer** — permanent badge: "🛡️ 100% private — everything runs on your device"
- **Dashboard hero** — pill badge: "🛡️ 100% Private — Nothing leaves your device"
- **Chat welcome** — "Everything stays private — nothing is sent online."
- **Settings page** — "🛡️ All processing is local. No telemetry. No cloud."

### Non-Blocking Feedback

- **Toast notifications** instead of `alert()` — less disruptive
- **Inline status text** instead of modal spinners — "⏳ Generating..."
- **Copy confirmation** via toast ("Copied to clipboard!") — no popup
- **Streaming responses** — tokens appear as they're generated, not all-at-once
- **Per-message actions** — Copy/Read Aloud buttons appear on hover, not always visible

### Cross-Feature Integration

Features connect to each other naturally, reducing context-switching:

- **Vision → Chat** — "Continue in Chat" button injects image analysis into chat
  history for follow-up questions
- **Transcription → SOAP** — "Generate SOAP Notes" button sends transcript to LLM
- **Medical Notes → Word** — one-click export to Word document for clinical workflows
- **Any result → TTS** — Read Aloud buttons on chat messages, vision results, and
  medical notes analysis
- **Chunked processing** — large documents (>8K chars) are automatically split,
  analyzed in sections, and merged — users don't need to know about context limits

### Theme Support

Light and dark themes with instant switching:

- Toggle button (☀️/🌙) in sidebar header
- Preference saved to `localStorage`
- All colors defined via CSS custom properties — `[data-theme="light"]` overrides

### Mobile Support

The sidebar collapses into a slide-out drawer on small screens. A hamburger
button (☰) appears in the top-left. Tapping a nav item closes the drawer
automatically. Cards and buttons stack vertically on phones.

---

## Modifying the UI

### Changing Colors

Edit the CSS custom properties in `:root` at the top of `www/css/style.css`.
All components reference these variables, so a single change propagates
everywhere.

### Adding a New Page

1. Add a `<div class="page" id="page-mypage">` in `index.html`
2. Add a `<a class="nav-item" data-page="mypage">` in the sidebar nav
3. Add a `setupMyPage()` function in `app.js` and call it from `init()`
4. The SPA router handles the rest automatically

### Adding a New API Endpoint

1. Add an exported function in `api.js` (follow the existing `fetch()` pattern)
2. Call it from the relevant page handler in `app.js`

---

## Serving the UI

The API server (`api_server.cpp`) auto-discovers the `www/` directory:

```
Search order:  ./www  →  ../www  (relative to the binary)
```

If found, it mounts the directory at `/` via `httplib::Server::set_mount_point`.
The API endpoints under `/v1/` take precedence.

For the Windows installer, `www/` is bundled alongside the binary at
`{app}\www\` so it's always available.
