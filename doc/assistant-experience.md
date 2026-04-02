# EDGESCRIBE Personal Assistant Experience — Design Document

## Inspiration: OpenClaw

[OpenClaw](https://github.com/openclaw/openclaw) is a personal AI assistant that runs as a background daemon and connects to 20+ messaging channels (WhatsApp, Telegram, Slack, Discord, iMessage, etc.). Its key insight: **meet the user where they already are** — don't make them open a new app.

EDGESCRIBE is fundamentally different (on-device inference vs cloud APIs, medical-focused vs general chat), but we can borrow the **always-on, always-available** experience.

### What we DON'T want from OpenClaw
- ❌ Multi-channel messaging routing (clinics don't need WhatsApp bots)
- ❌ Cloud API dependency (contradicts our on-device value proposition)
- ❌ Node.js runtime (we're native C++)
- ❌ Complex plugin/skills framework (our features are built-in)

### What we DO want from OpenClaw
- ✅ Background service — always running, no manual launch
- ✅ System tray / menu bar — one-click access
- ✅ Hotkeys — start recording without switching windows
- ✅ Voice wake word — "Hey Scribe, start recording"
- ✅ Notifications — "SOAP notes ready"

---

## The Goal: From Tool to Assistant

### Current UX (v1.0) — "A tool you launch"

```
Doctor wants to transcribe:
  1. Open terminal
  2. Type: edgescribe run --live
  3. Speak
  4. Ctrl+C to stop
  5. Copy output

Doctor wants SOAP notes:
  1. Open terminal
  2. Type: edgescribe process --soap transcript.txt
  3. Wait
  4. Copy output
```

### Target UX (v1.2+) — "An assistant that's always there"

```
Doctor wants to transcribe:
  1. Press Ctrl+Shift+R (or click tray icon)
  2. Speak
  3. Press hotkey again to stop
  4. Notification: "Transcript saved. Generate SOAP notes?"
  5. Click notification → SOAP notes appear in browser

Doctor wants SOAP notes:
  1. Click tray icon → "Generate SOAP notes from last session"
  2. Done — notification when ready
```

---

## Feature 1: Background Service (Daemon)

EDGESCRIBE starts on login and runs in the background. No terminal needed.

### Windows: Startup + System Tray

```
Installation adds:
  1. Start Menu shortcut
  2. Startup entry (Registry or Task Scheduler)
  3. System tray icon (notification area)

On boot:
  → edgescribe serve starts automatically
  → System tray icon appears (🩺)
  → Web UI available at localhost:8080
  → Models lazy-loaded on first request
```

**Implementation options:**

| Approach | Effort | UX |
|---|---|---|
| **Task Scheduler + tray app** | Medium | Runs on login, tray icon via separate small app |
| **Windows Service** | High | Runs as NT service, no tray (need separate tray app) |
| **Inno Setup startup entry** | Low | Add to registry Run key during installation |

**Recommended for v1.2**: Inno Setup already supports adding startup entries. Add a small tray app (can be a simple C++ app using Win32 `Shell_NotifyIcon`).

### macOS: launchd + Menu Bar

```
Installation adds:
  ~/Library/LaunchAgents/ai.edgescribe.server.plist

On login:
  → launchd starts edgescribe serve
  → Menu bar icon appears (🩺)
  → Web UI available at localhost:8080
```

**launchd plist:**
```xml
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
    <key>Label</key>
    <string>ai.edgescribe.server</string>
    <key>ProgramArguments</key>
    <array>
        <string>/usr/local/bin/edgescribe</string>
        <string>serve</string>
        <string>--port</string>
        <string>8080</string>
    </array>
    <key>RunAtLoad</key>
    <true/>
    <key>KeepAlive</key>
    <true/>
</dict>
</plist>
```

---

## Feature 2: System Tray / Menu Bar

Quick access without opening a browser or terminal.

### Tray Menu

```
🩺 EDGESCRIBE                        ← Icon in system tray / menu bar
├── 🎤 Start Recording               ← Starts live transcription
├── 📋 Last Transcript                ← Opens last session in browser
├── 💬 Open Chat                      ← Opens web UI chat page
├── ─────────────                     
├── 📊 Status: Ready (2 GB RAM)      ← Shows engine status
├── ⚙️ Settings                       ← Opens settings in browser
├── ─────────────
└── ❌ Quit                           ← Stops server
```

### Implementation

**Windows** — Win32 `Shell_NotifyIcon` API (~200 lines of C++):
```cpp
// Small separate executable or integrated into edgescribe serve
NOTIFYICONDATA nid = {};
nid.cbSize = sizeof(nid);
nid.uFlags = NIF_ICON | NIF_TIP | NIF_MESSAGE;
nid.hIcon = LoadIcon(hInstance, MAKEINTRESOURCE(IDI_TRAY));
wcscpy_s(nid.szTip, L"EDGESCRIBE — Ready");
Shell_NotifyIcon(NIM_ADD, &nid);
```

**macOS** — NSStatusItem (~100 lines of Objective-C++):
```objc
NSStatusItem* statusItem = [[NSStatusBar systemStatusBar]
    statusItemWithLength:NSVariableStatusItemLength];
statusItem.button.title = @"🩺";
statusItem.menu = trayMenu;
```

---

## Feature 3: Global Hotkeys

Control EDGESCRIBE without switching windows.

| Hotkey | Action |
|---|---|
| `Ctrl+Shift+R` | Start/stop recording |
| `Ctrl+Shift+S` | Generate SOAP from last transcript |
| `Ctrl+Shift+C` | Open chat in browser |
| `Ctrl+Shift+Space` | Push-to-talk (hold to record, release to stop) |

### Implementation

**Windows** — `RegisterHotKey` API:
```cpp
RegisterHotKey(hwnd, HOTKEY_RECORD, MOD_CONTROL | MOD_SHIFT, 'R');
// In message loop: WM_HOTKEY → toggle recording via API call
```

**macOS** — `CGEventTap` or `NSEvent.addGlobalMonitorForEvents`:
```objc
[NSEvent addGlobalMonitorForEventsMatchingMask:NSEventMaskKeyDown
    handler:^(NSEvent* event) {
        if (event.modifierFlags & NSEventModifierFlagCommand &&
            event.keyCode == kVK_ANSI_R) {
            // Toggle recording via HTTP call to localhost:8080
        }
    }];
```

---

## Feature 4: Voice Wake Word (v1.3+)

"Hey Scribe" → starts recording automatically.

### How it works

```
Microphone always listening (low CPU, tiny model)
    ↓
Wake word detected: "Hey Scribe"
    ↓
Start full ASR transcription
    ↓
Silence detected (3 seconds) → stop
    ↓
Notification: "Transcript ready"
```

### Implementation options

| Approach | Model Size | CPU Usage | Quality |
|---|---|---|---|
| **Porcupine** (Picovoice) | ~2 MB | ~1% CPU | ✅ Best, but proprietary |
| **OpenWakeWord** | ~10 MB | ~2% CPU | ✅ Good, open-source |
| **Simple energy detection** | 0 MB | <1% CPU | ⚠️ No wake word, just "noise → start" |

**Recommended**: Start with simple energy-based voice activity detection (VAD) — when the user starts speaking, begin transcription. Add a proper wake word model later.

---

## Feature 5: Desktop Notifications

Push notifications for completed tasks.

```
┌──────────────────────────────────┐
│ 🩺 EDGESCRIBE                    │
│                                  │
│ Transcription complete (5:23)    │
│ 📝 Generate SOAP notes?         │
│                                  │
│ [SOAP Notes]  [Dismiss]          │
└──────────────────────────────────┘
```

### Implementation

**Windows** — `ToastNotification` API (Win10+):
```cpp
// Or simpler: Shell_NotifyIcon with NIF_INFO balloon
nid.uFlags = NIF_INFO;
wcscpy_s(nid.szInfoTitle, L"Transcription Complete");
wcscpy_s(nid.szInfo, L"5:23 session recorded. Click to generate SOAP notes.");
Shell_NotifyIcon(NIM_MODIFY, &nid);
```

**macOS** — `NSUserNotification` or `UNUserNotificationCenter`:
```objc
UNMutableNotificationContent* content = [[UNMutableNotificationContent alloc] init];
content.title = @"Transcription Complete";
content.body = @"5:23 session recorded. Click to generate SOAP notes.";
```

---

## Implementation Roadmap

### v1.0 (current) — Tool mode
```
✅ CLI commands
✅ Web UI via edgescribe serve
✅ User manually starts/stops everything
```

### v1.2 — Background service + tray
```
- Auto-start on login (Windows Task Scheduler / macOS launchd)
- System tray icon with quick-access menu
- Desktop notifications for completed tasks
- Global hotkey: Ctrl+Shift+R to start/stop recording
```

### v1.3 — Push-to-talk + voice activation
```
- Push-to-talk hotkey (hold to record, release to stop)
- Voice activity detection (auto-start on speech)
- Smart silence detection (auto-stop after pause)
```

### v2.0 — Wake word + companion apps
```
- "Hey Scribe" wake word (OpenWakeWord)
- macOS menu bar native app
- Mobile companion (connect to desktop EDGESCRIBE over LAN)
```

---

## Architecture for Always-On

```
┌─────────────────────────────────────────────┐
│  edgescribe serve (background daemon)        │
│  ├── HTTP server (localhost:8080)             │
│  ├── LLM engine (lazy-loaded)                │
│  ├── ASR engine (lazy-loaded)                │
│  ├── TTS engine (lazy-loaded)                │
│  ├── Vision engine (lazy-loaded)             │
│  ├── MemoryStore (SQLite, always active)     │
│  └── Idle monitor (unloads unused engines)   │
│                                              │
│  RAM when idle: ~10 MB (no models loaded)    │
│  RAM when active: ~2 GB (models loaded)      │
│  Models auto-unload after 5 min idle         │
└──────────────┬──────────────────────────────┘
               │
    ┌──────────┼──────────────┐
    │          │              │
┌───▼───┐ ┌───▼───┐    ┌─────▼─────┐
│ Tray  │ │ Web   │    │ Hotkeys   │
│ Icon  │ │ UI    │    │ (global)  │
└───────┘ └───────┘    └───────────┘
```

The server already supports lazy-loading and idle unloading — it's already designed for always-on operation. The missing pieces are the tray icon, hotkeys, and auto-start — all platform-specific glue code, not inference changes.

---

## Comparison: OpenClaw vs EDGESCRIBE Assistant

| Aspect | OpenClaw | EDGESCRIBE (target) |
|---|---|---|
| **Always-on** | ✅ Node.js daemon | ✅ C++ daemon (lighter) |
| **Tray/Menu bar** | ✅ macOS app | ✅ Windows tray + macOS menu bar |
| **Hotkeys** | ❌ Not built-in | ✅ Ctrl+Shift+R for recording |
| **Wake word** | ✅ Voice Wake | 🔜 "Hey Scribe" (v2.0) |
| **Channels** | ✅ 20+ messaging apps | ❌ Web UI + CLI only |
| **Privacy** | ⚠️ Cloud LLMs | ✅ 100% on-device |
| **Notifications** | ✅ Push notifications | ✅ Desktop notifications |
| **Mobile** | ✅ iOS/Android app | 🔜 Mobile browser over LAN |

EDGESCRIBE doesn't need 20 messaging channels. It needs to be **instantly accessible** from the doctor's desk — tray icon, hotkey, done.
