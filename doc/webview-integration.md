# Native GUI Integration (webview)

EDGESCRIBE can run as a **native desktop application** — no browser required.
The `edgescribe gui` command opens the web UI inside a native window using the
operating system's built-in browser engine.

## How It Works

```
┌──────────────────────────────────────────────────────────┐
│  Native Window (webview)                                 │
│  ┌────────────────────────────────────────────────────┐  │
│  │                                                    │  │
│  │   EDGESCRIBE Web UI  (www/index.html)              │  │
│  │   served by the built-in HTTP server               │  │
│  │                                                    │  │
│  │   HTML/CSS/JS ←→ fetch() ←→ localhost:8080/v1/     │  │
│  │                                                    │  │
│  └────────────────────────────────────────────────────┘  │
│                           ↕                              │
│  ┌────────────────────────────────────────────────────┐  │
│  │  EDGESCRIBE C++ binary (same process)              │  │
│  │  ├── HTTP Server (httplib)  ← serves www/ + API    │  │
│  │  ├── ASR Engine (Nemotron)                         │  │
│  │  ├── LLM Engine (Qwen3-VL)                         │  │
│  │  ├── Vision Engine (Qwen3-VL)                      │  │
│  │  └── TTS Engine (Kokoro)                           │  │
│  └────────────────────────────────────────────────────┘  │
└──────────────────────────────────────────────────────────┘
```

When you run `edgescribe gui`:

1. The REST API server starts in a **background thread** on `localhost:8080`
2. A **native window** opens pointing at `http://localhost:8080`
3. The web UI loads and talks to the API via `fetch()` — same as in a browser
4. When you **close the window**, the server shuts down automatically

## Platform Browser Engines

| Platform | Engine | Notes |
|----------|--------|-------|
| Windows  | Edge WebView2 (Chromium) | Pre-installed on Win10 Nov 2021+ and Win11 |
| macOS    | WKWebView (WebKit/Safari) | Built into macOS — no extra install |
| Linux    | WebKitGTK | Requires `libwebkit2gtk-4.0-dev` package |

## Usage

```bash
# Open the app as a native window (recommended for end users)
edgescribe gui

# Customize the port
edgescribe gui --port 9090

# Specify a device (GPU acceleration)
edgescribe gui --device cuda
```

All flags from `edgescribe serve` also work with `edgescribe gui`:

| Flag | Default | Description |
|------|---------|-------------|
| `--port` | `8080` | HTTP server port |
| `--host` | `127.0.0.1` | Bind address |
| `--device` | `cpu` | Execution provider (cpu, cuda, dml, vulkan) |
| `--asr-model` | auto | Path to ASR model (auto-resolved from cache) |
| `--vlm-model` | auto | Path to Vision/LLM model |
| `--tts-model` | auto | Path to TTS model |

## Building with GUI Support

GUI support is **optional** and controlled by the `EDGESCRIBE_ENABLE_GUI`
CMake option. When disabled (the default), the `gui` command is not available
and no webview dependency is needed.

### Step 1: Download webview.h

```bash
curl -sL -o include/webview/webview.h \
  https://raw.githubusercontent.com/webview/webview/master/webview.h
```

### Step 2: Platform-specific setup

**Windows** — download the WebView2 SDK:

```powershell
nuget install Microsoft.Web.WebView2 -OutputDirectory deps
```

**macOS** — nothing extra needed.

**Linux** — install WebKitGTK:

```bash
sudo apt install libwebkit2gtk-4.0-dev    # Debian/Ubuntu
sudo dnf install webkit2gtk4.0-devel      # Fedora
sudo pacman -S webkit2gtk                 # Arch
```

### Step 3: Configure and build

```bash
# Windows (with WebView2 SDK)
cmake -B build -DCMAKE_BUILD_TYPE=Release \
  -DORT_GENAI_PATH=/path/to/onnxruntime-genai \
  -DEDGESCRIBE_ENABLE_GUI=ON \
  -DWEBVIEW2_PATH=deps/Microsoft.Web.WebView2.1.x.x

# macOS / Linux (no extra paths needed)
cmake -B build -DCMAKE_BUILD_TYPE=Release \
  -DORT_GENAI_PATH=/path/to/onnxruntime-genai \
  -DEDGESCRIBE_ENABLE_GUI=ON

# Build
cmake --build build --config Release
```

### Step 4: Run

```bash
./build/edgescribe gui
```

## Installer (Windows)

When built with GUI support, the Windows installer (`EDGESCRIBESetup.exe`):

- Creates a **desktop shortcut** that runs `edgescribe gui`
- Creates a **Start Menu entry** under "EDGESCRIBE"
- Bundles `WebView2Loader.dll` alongside the main binary
- Bundles the `www/` directory with all frontend assets
- Offers to launch the app immediately after installation

Users simply double-click the desktop icon to start EDGESCRIBE — no terminal,
no browser, no technical knowledge required.

## Architecture Notes

- The webview library is **header-only** (`webview.h`), consistent with the
  project's pattern for httplib.h and miniaudio.h.
- The GUI module lives in `src/gui/` and is conditionally compiled via
  `EDGESCRIBE_HAS_GUI`.
- The `gui` command reuses the same `ServerConfig` and `ApiServer` as the
  `serve` command — no code duplication.
- No Electron, no bundled browser, no Node.js. The native OS browser engine
  adds **zero binary size** overhead.

## Troubleshooting

**Windows: "WebView2 Runtime not found"**
The WebView2 runtime ships with Windows 10 (Nov 2021 update) and Windows 11.
If you're on an older build, download the runtime from:
https://developer.microsoft.com/en-us/microsoft-edge/webview2/

**Linux: "WebKitGTK not found"**
Install the development package for your distribution (see above).

**Port already in use**
Use `--port` to pick a different port: `edgescribe gui --port 9090`
