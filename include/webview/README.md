# webview — Native GUI dependency

This directory holds the [webview](https://github.com/webview/webview) header
used by `edgescribe gui` to open the web UI in a native desktop window.

## Setup

### 1. Download webview.h

```bash
# From the repo root
curl -sL -o include/webview/webview.h \
  https://raw.githubusercontent.com/webview/webview/master/webview.h
```

### 2. Windows — WebView2 SDK

On Windows the webview library uses Microsoft Edge WebView2.
Download the NuGet package and tell CMake where it is:

```powershell
# Download the WebView2 SDK NuGet package
nuget install Microsoft.Web.WebView2 -OutputDirectory deps

# Or download manually from:
# https://www.nuget.org/packages/Microsoft.Web.WebView2

# Then configure CMake:
cmake -B build -DEDGESCRIBE_ENABLE_GUI=ON ^
  -DWEBVIEW2_PATH=deps/Microsoft.Web.WebView2.x.x.x ^
  -DORT_GENAI_PATH=...
```

The WebView2 **runtime** is pre-installed on Windows 10 (Nov 2021+) and
all Windows 11 machines. End users do not need to install anything extra.

### 3. macOS

No extra dependencies. WebKit (WKWebView) is built into macOS.

### 4. Linux

Install the WebKitGTK development package:

```bash
# Debian / Ubuntu
sudo apt install libwebkit2gtk-4.0-dev

# Fedora
sudo dnf install webkit2gtk4.0-devel

# Arch
sudo pacman -S webkit2gtk
```

## Files

| File | Source | Purpose |
|------|--------|---------|
| `webview.h` | [webview/webview](https://github.com/webview/webview) | Single-header webview library |
| `README.md` | This file | Setup instructions |

The `webview.h` file is **not committed to the repo** (gitignored).
Download it at build time using the curl command above.
