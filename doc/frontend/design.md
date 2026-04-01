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
    └── api.js          # API client — thin fetch() wrappers for /v1/ endpoints
```

All frontend logic lives in two JS files:
- **`api.js`** handles HTTP calls (what to send)
- **`app.js`** handles UI behavior (what to show)

---

## Pages

### 🏠 Home (Dashboard)

The landing page shown on launch.

- **Welcome hero** — greeting with a description of what the app does
- **Privacy pill** — prominent badge: "🛡️ 100% Private — Nothing leaves your device"
- **Status cards** — clickable tiles for each AI engine, showing ready/not-available
- **Quick actions** — large buttons to jump to the most common tasks

### 🎤 Record & Transcribe

Real-time speech-to-text from the user's microphone.

- **Record button** — large red circle; click to start/stop, pulses while recording
- **Live transcript** — text appears in real time with a blinking cursor
- **Stats bar** — elapsed time and chunk count
- **Actions** — Copy text, Save as .txt file, Generate SOAP notes, Clear

Audio flows as: `Mic → Web Audio API (16 kHz PCM) → POST /v1/transcribe/push → display`

### 📁 Upload Audio

Transcribe a pre-recorded audio file.

- **Drop zone** — drag-and-drop area with click-to-browse fallback
- **Progress bar** — gradient-filled bar during processing
- **Transcript area** — result displayed with copy/save buttons
- Supports WAV, MP3, and FLAC

### 💬 Chat

Conversational AI powered by the on-device LLM.

- **Message thread** — scrolling list of user/assistant messages with avatars
- **Input area** — auto-growing textarea with send button
- **Keyboard shortcut** — Enter to send, Shift+Enter for new line
- Welcome message explains capabilities and privacy

### 🖼️ Image Analysis

Upload a photo for AI description or text extraction (OCR).

- **Two-column layout** — image + prompt on the left, result on the right
- **Two actions** — "Analyze Image" (general description) and "Extract Text (OCR)"
- **Custom prompt** — user can ask specific questions about the image

### 🔊 Read Aloud

Convert text to natural-sounding speech.

- **Text input** — large textarea for typing or pasting
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
| Card | `.card` | Rounded container with subtle border and hover effect |
| Status card | `.stat-card` | Dashboard tile with icon, label, status; lifts on hover |
| Button | `.btn`, `.btn-primary`, `.btn-lg` | Rounded buttons; primary uses gradient accent |
| Record button | `.record-btn` | Red-bordered circle with pulse animation while recording |
| Transcript area | `.transcript-area` | Scrollable text display with blinking cursor |
| Drop zone | `.drop-zone` | Dashed-border area for drag-and-drop file upload |
| Toast | `.toast` | Slide-in notification (success/error/info); auto-dismisses |
| Privacy badge | `.privacy-badge` | Green-tinted badge emphasizing on-device processing |
| Welcome hero | `.welcome-hero` | Gradient-background hero section on the dashboard |

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
| `≤ 768px` | Sidebar becomes a slide-out drawer with hamburger toggle; reduced padding |
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
chat(prompt)            → POST /v1/chat                (JSON)
generateSoap(text)      → POST /v1/chat/soap           (JSON)
summarize(text)         → POST /v1/chat/summarize      (JSON)
analyzeImage(file, p)   → POST /v1/vision/analyze      (multipart)
ocrImage(file)          → POST /v1/vision/ocr           (multipart)
synthesize(text)        → POST /v1/tts/synthesize      (JSON → WAV blob)
listVoices()            → GET  /v1/tts/voices
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
| `setupChat()` | Message threading, input handling, send on Enter |
| `setupVision()` | Image upload, analyze/OCR buttons, result display |
| `setupTTS()` | Text input, play/save audio |

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
