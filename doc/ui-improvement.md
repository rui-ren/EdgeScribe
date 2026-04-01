# EDGESCRIBE Web UI — Improvement Plan

## Current State

EDGESCRIBE ships a web dashboard served directly by the C++ HTTP server (`httplib.h`). Vanilla HTML/CSS/JS — no framework, no build step. Located in `www/`.

### What we already have
- Dark mode sidebar navigation with icons
- Dashboard with stat cards and quick actions
- Live transcription page with record button + streaming
- Chat interface with message bubbles
- Vision/OCR page with image upload
- TTS page with text input + playback
- Settings page with model status table
- Engine status indicators in sidebar footer (ASR, LLM, Vision, TTS)
- Privacy badge ("100% private — everything runs on your device")
- Toast notifications
- Mobile responsive (sidebar toggle)

### What's missing (inspired by LM Studio)

LM Studio (Electron desktop app, llama.cpp under the hood) has a polished UI with features that make local AI feel professional. We can adapt the best ideas for our web dashboard without adding Electron overhead.

---

## Priority 1: High Impact, Low Effort

### 1. Live Stats Bar

Show real-time inference stats in a subtle footer or header bar. Makes the app feel responsive and professional.

```
┌─────────────────────────────────────────────────────────────┐
│  ⚡ 22 tok/s  │  📊 1.8 GB RAM  │  🎯 842/16384 context   │
└─────────────────────────────────────────────────────────────┘
```

**What to show:**
- Tokens per second (during generation)
- Memory usage (model + KV cache)
- Context usage (tokens used / n_ctx)
- Active engine indicator

**Backend needed:** Add `GET /v1/stats` endpoint in `api_server.cpp` returning:
```json
{
  "tokens_per_second": 22.4,
  "memory_mb": 1843,
  "context_used": 842,
  "context_max": 16384,
  "active_engine": "llm"
}
```

**Frontend:** Poll `/v1/stats` every 1-2 seconds during generation. Display in a fixed footer bar.

---

### 2. In-UI Model Management

Replace the static model status table in Settings with interactive model cards that let users download, remove, and see download progress — all from the browser.

**Current (static):**
```
| Model    | Powers             | Size     | Status     |
|----------|--------------------|----------|------------|
| Nemotron | Speech Recognition | ~670 MB  | ✅ Ready   |
| Qwen3-VL | Chat & Vision     | ~1.5 GB  | ❌ Not installed |
```

**Improved (interactive):**
```
┌─────────────────────────────────────────┐
│  🎤 Nemotron (ASR)                      │
│  Parakeet TDT 0.6B — ~670 MB           │
│  Status: ✅ Installed                   │
│  [Remove]                               │
├─────────────────────────────────────────┤
│  🧠 Qwen3-VL (Chat & Vision)           │
│  Qwen3-VL-2B INT4 — ~1.5 GB            │
│  Status: ❌ Not installed               │
│  [Download]                             │
│  ████████░░░░░░░░░░ 45% (675 MB / 1.5 GB) │
└─────────────────────────────────────────┘
```

**Backend needed:**
- `POST /v1/models/pull` — start download (already have `pull` in CLI, expose via API)
- `GET /v1/models/pull/status` — download progress (percentage, bytes downloaded)
- `DELETE /v1/models/:name` — remove a model

**Frontend:** Card-based layout with download progress bar, install/remove buttons.

---

### 3. Chat Export

One-click export of chat conversations. Essential for clinical workflows where users need to save or share LLM output.

**UI:**
```
[📋 Copy] [📄 Save as Markdown] [📑 Save as PDF] [🗑 Clear Chat]
```

**Implementation:** Pure frontend — no backend changes. Build markdown/text from the chat message DOM, trigger download via `Blob` + `URL.createObjectURL`.

```javascript
function exportChatMarkdown() {
  const messages = document.querySelectorAll('.message');
  let md = '# EDGESCRIBE Chat Export\n\n';
  messages.forEach(msg => {
    const role = msg.querySelector('.assistant') ? 'Assistant' : 'You';
    const text = msg.querySelector('.message-content').textContent;
    md += `**${role}:** ${text}\n\n`;
  });
  downloadFile('chat-export.md', md);
}
```

---

### 4. System Prompt Editor

Add a collapsible "System Prompt" field above the chat input. Lets users customize the AI's behavior without editing code.

**UI (collapsed):**
```
[⚙️ System Prompt ▼]
```

**UI (expanded):**
```
[⚙️ System Prompt ▲]
┌─────────────────────────────────────────────────────────────┐
│ You are a medical assistant. Answer questions about         │
│ clinical guidelines and patient care.                       │
└─────────────────────────────────────────────────────────────┘
```

**Backend:** Already supported — `/v1/chat` accepts `"system"` field in JSON. Just need to send it from the frontend.

---

## Priority 2: Medium Impact, Medium Effort

### 5. Drag-and-Drop Images into Chat

Unify the chat and vision experiences. Instead of navigating to a separate Vision page, users can drop an image directly into the chat and ask questions about it.

**UX flow:**
```
User drags image into chat → preview appears in chat bubble
User types: "What does this document say?"
→ POST /v1/vision/analyze (multipart: image + prompt)
→ Response appears as assistant message in chat
```

**Frontend changes:**
- Add drag-and-drop listener on chat area
- Show image preview in a user message bubble
- Route to vision API instead of chat API when image is attached

**Keep the standalone Vision page** for users who prefer the dedicated layout.

---

### 6. Conversation History (ties into v1.2 Memory)

Save and reload past chat sessions. Depends on the SQLite memory system (v1.2).

**Sidebar addition:**
```
💬 Chat
   ├── New Chat
   ├── Today
   │   ├── Patient consult — diabetes
   │   └── Drug interaction question
   └── Yesterday
       └── SOAP notes review
```

**Backend needed:** Memory system endpoints (v1.2):
- `GET /v1/memory/sessions` — list past sessions
- `GET /v1/memory/sessions/:id` — load session messages

**Defer to v1.2** — requires persistent storage.

---

### 7. Streaming Token Animation

Improve the chat typing experience with a cursor/caret animation during token streaming.

**Current:** Text appears chunk by chunk (functional but abrupt)

**Improved:**
```
Assistant: The patient should continue with metformin 500mg BID▌
                                                              ↑ blinking cursor
```

**Implementation:** CSS animation on a `::after` pseudo-element during streaming:
```css
.message-content.streaming::after {
  content: '▌';
  animation: blink 0.8s infinite;
}
@keyframes blink {
  0%, 100% { opacity: 1; }
  50% { opacity: 0; }
}
```

---

## Priority 3: Nice to Have

### 8. Keyboard Shortcuts

| Shortcut | Action |
|---|---|
| `Ctrl+Enter` | Send message (alternative to Enter) |
| `Ctrl+Shift+N` | New chat |
| `Ctrl+Shift+R` | Start/stop recording |
| `Ctrl+Shift+C` | Copy last response |
| `Escape` | Stop generation |

### 9. Theme Toggle (Light/Dark)

Currently dark-only. Add a toggle in settings for light mode. Store preference in `localStorage`.

### 10. Audio Waveform Visualizer

During live transcription, show a real-time audio waveform instead of just the record button. Uses Web Audio API `AnalyserNode`.

```
[●] Recording...  ▁▂▃▅▇▅▃▂▁▂▃▅▇▅▃▂▁
```

### 11. Markdown Rendering in Chat

Render LLM output as formatted markdown (headers, bold, lists, code blocks) instead of plain text. Use a lightweight library like `marked.js` (~8 KB gzipped).

### 12. Context Window Indicator

Visual bar showing how much of the context window is used:

```
Context: [██████████░░░░░░░░░░] 5,200 / 16,384 tokens (32%)
         ↑ system  ↑ RAG    ↑ chat     ↑ available
```

Color-coded segments: system prompt (blue), RAG context (green), user messages (white), available (gray).

---

## What NOT to Add

| LM Studio Feature | Why Skip |
|---|---|
| **Split-view multi-chat** | Overkill for clinical workflows. One focused conversation is better. |
| **Electron wrapper** | Adds ~300 MB overhead. Web UI served by httplib.h is lighter and faster. |
| **General model browser** | We have 3 curated models, not a marketplace. Keep it simple. |
| **GPU layer slider** | Future (v1.4+). CPU-only for v1. |
| **Plugin system** | Over-engineering. Features are built-in. |

---

## Implementation Order

**v1.0 (ship now):**
- Current UI is good enough. Ship it.

**v1.1 (quick wins — frontend only, no backend changes):**
- Chat export (copy/markdown/PDF)
- System prompt editor
- Streaming cursor animation
- Keyboard shortcuts

**v1.2 (with memory system):**
- Conversation history sidebar
- In-UI model management (needs new API endpoints)
- Live stats bar (needs `/v1/stats` endpoint)

**v1.3+ (polish):**
- Drag-and-drop images into chat
- Markdown rendering
- Audio waveform visualizer
- Context window indicator
- Light/dark theme toggle

---

## Design Principles

1. **No build step** — keep vanilla HTML/CSS/JS. No React, no Webpack, no npm.
2. **No heavy libraries** — if we add a library, it must be < 20 KB gzipped (e.g., `marked.js` for markdown).
3. **Mobile-friendly** — all improvements must work on tablet/phone (responsive).
4. **Accessible** — keyboard navigable, screen reader friendly, proper ARIA labels.
5. **Fast** — no lazy loading, no spinners for < 100ms operations. The UI should feel instant.
6. **Privacy-first** — no external CDN links, no Google Fonts, no analytics. Everything self-contained.
