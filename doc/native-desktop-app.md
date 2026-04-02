# EDGESCRIBE Native Desktop App — WebView Design

## Overview

Replace the "open browser tab" experience with a **native desktop window** that embeds the web UI. Users see a desktop app called "EDGESCRIBE" — no browser chrome, no URL bar, no tabs.

**Zero extra download** — uses OS-native WebView components:
- **Windows 10/11**: WebView2 (Microsoft Edge runtime, pre-installed)
- **macOS**: WKWebView (part of macOS, always available)

---

## User Experience

### Before (v1.0 — browser tab)

```
User clicks desktop icon
  → Terminal flashes briefly
  → Chrome/Edge opens with new tab
  → URL bar shows: http://localhost:8080     ← scary for non-tech users
  → Browser tab title: "EDGESCRIBE — On-Device AI Assistant"
  → Mixed in with user's other 47 browser tabs
```

### After (v1.2 — native window)

```
User clicks desktop icon "🩺 EDGESCRIBE"
  → Native window appears (no terminal)
  → Title bar: "EDGESCRIBE"                 ← clean
  → No URL bar, no tabs, no browser UI
  → Standalone app in taskbar/dock
  → Feels like LM Studio / Jan.ai
```

---

## Architecture

```
┌──────────────────────────────────────────────────┐
│  edgescribe gui                                   │
│                                                   │
│  ┌─────────────────┐    ┌──────────────────────┐  │
│  │  HTTP Server     │    │  WebView Window      │  │
│  │  (httplib.h)     │◄──►│  (WebView2/WKWebView)│  │
│  │  localhost:8080  │    │  loads localhost:8080 │  │
│  │                  │    │  no URL bar           │  │
│  │  Same server as  │    │  native title bar     │  │
│  │  edgescribe serve│    │  system tray icon     │  │
│  └─────────────────┘    └──────────────────────┘  │
│                                                   │
│  Single process — server + window in one binary   │
└──────────────────────────────────────────────────┘
```

### How `edgescribe gui` works

```
1. Start HTTP server on localhost:8080 (background thread)
2. Wait for server to be ready
3. Create native window with WebView
4. Load http://localhost:8080 in the WebView
5. Show system tray icon
6. Block on window event loop
7. On window close → stop server → exit
```

---

## Windows Implementation (WebView2)

### Prerequisites

WebView2 runtime is **pre-installed on Windows 10 (April 2021+) and all Windows 11**. For older systems, the Inno Setup installer can bundle the bootstrapper (~1.5 MB).

### Code (~80 lines)

```cpp
// src/gui/gui_launcher_win.cpp

#include <windows.h>
#include <wrl.h>
#include <WebView2.h>
#include <WebView2EnvironmentOptions.h>

#pragma comment(lib, "WebView2Loader.dll.lib")

using namespace Microsoft::WRL;

static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM w, LPARAM l) {
    switch (msg) {
        case WM_SIZE: {
            // Resize WebView to fill window
            auto* webview = reinterpret_cast<ICoreWebView2Controller*>(
                GetWindowLongPtr(hwnd, GWLP_USERDATA));
            if (webview) {
                RECT bounds;
                GetClientRect(hwnd, &bounds);
                webview->put_Bounds(bounds);
            }
            return 0;
        }
        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProc(hwnd, msg, w, l);
}

int LaunchGui(int port) {
    // Register window class
    WNDCLASSEX wc = {};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = WndProc;
    wc.hInstance = GetModuleHandle(nullptr);
    wc.lpszClassName = L"EDGESCRIBE";
    wc.hIcon = LoadIcon(wc.hInstance, MAKEINTRESOURCE(101));
    RegisterClassEx(&wc);

    // Create window
    HWND hwnd = CreateWindowEx(
        0, L"EDGESCRIBE", L"EDGESCRIBE",
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, 1280, 800,
        nullptr, nullptr, wc.hInstance, nullptr);

    ShowWindow(hwnd, SW_SHOW);
    UpdateWindow(hwnd);

    // Create WebView2
    CreateCoreWebView2EnvironmentWithOptions(
        nullptr, nullptr, nullptr,
        Callback<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler>(
            [hwnd, port](HRESULT, ICoreWebView2Environment* env) -> HRESULT {
                env->CreateCoreWebView2Controller(hwnd,
                    Callback<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler>(
                        [hwnd, port](HRESULT, ICoreWebView2Controller* controller) -> HRESULT {
                            // Store controller for resize handling
                            SetWindowLongPtr(hwnd, GWLP_USERDATA,
                                reinterpret_cast<LONG_PTR>(controller));

                            // Resize to fill window
                            RECT bounds;
                            GetClientRect(hwnd, &bounds);
                            controller->put_Bounds(bounds);

                            // Navigate to local server
                            ComPtr<ICoreWebView2> webview;
                            controller->get_CoreWebView2(&webview);

                            // Hide status bar and other browser chrome
                            ComPtr<ICoreWebView2Settings> settings;
                            webview->get_Settings(&settings);
                            settings->put_IsStatusBarEnabled(FALSE);
                            settings->put_AreDefaultContextMenusEnabled(FALSE);

                            std::wstring url = L"http://localhost:" +
                                std::to_wstring(port);
                            webview->Navigate(url.c_str());

                            return S_OK;
                        }).Get());
                return S_OK;
            }).Get());

    // Message loop
    MSG msg;
    while (GetMessage(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    return 0;
}
```

### CMake for WebView2

```cmake
# Download WebView2 NuGet package
if(WIN32 AND EDGESCRIBE_BUILD_GUI)
    # WebView2 SDK (headers + loader DLL)
    # Download from: https://www.nuget.org/packages/Microsoft.Web.WebView2
    find_path(WEBVIEW2_INCLUDE_DIR
        NAMES WebView2.h
        PATHS "${CMAKE_SOURCE_DIR}/include/WebView2/build/native/include")

    find_library(WEBVIEW2_LOADER
        NAMES WebView2Loader
        PATHS "${CMAKE_SOURCE_DIR}/include/WebView2/build/native/x64")

    target_include_directories(edgescribe PRIVATE ${WEBVIEW2_INCLUDE_DIR})
    target_link_libraries(edgescribe PRIVATE ${WEBVIEW2_LOADER})
    target_compile_definitions(edgescribe PRIVATE EDGESCRIBE_HAS_GUI)
endif()
```

---

## macOS Implementation (WKWebView)

### Prerequisites

WKWebView is part of macOS — always available. No downloads needed.

### Code (~60 lines)

```objc
// src/gui/gui_launcher_mac.mm

#import <Cocoa/Cocoa.h>
#import <WebKit/WebKit.h>

int LaunchGui(int port) {
    @autoreleasepool {
        [NSApplication sharedApplication];
        [NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];

        // Create window
        NSRect frame = NSMakeRect(0, 0, 1280, 800);
        NSWindow* window = [[NSWindow alloc]
            initWithContentRect:frame
            styleMask:(NSWindowStyleMaskTitled |
                       NSWindowStyleMaskClosable |
                       NSWindowStyleMaskResizable |
                       NSWindowStyleMaskMiniaturizable)
            backing:NSBackingStoreBuffered
            defer:NO];

        [window setTitle:@"EDGESCRIBE"];
        [window center];

        // Create WebView
        WKWebViewConfiguration* config = [[WKWebViewConfiguration alloc] init];
        WKWebView* webView = [[WKWebView alloc]
            initWithFrame:frame
            configuration:config];

        [window setContentView:webView];

        // Load local server
        NSString* urlStr = [NSString stringWithFormat:
            @"http://localhost:%d", port];
        NSURL* url = [NSURL URLWithString:urlStr];
        NSURLRequest* request = [NSURLRequest requestWithURL:url];
        [webView loadRequest:request];

        // Show window
        [window makeKeyAndOrderFront:nil];
        [NSApp activateIgnoringOtherApps:YES];

        // Set up app menu (Quit shortcut)
        NSMenu* menuBar = [[NSMenu alloc] init];
        NSMenuItem* appMenuItem = [[NSMenuItem alloc] init];
        [menuBar addItem:appMenuItem];
        NSMenu* appMenu = [[NSMenu alloc] init];
        [appMenu addItemWithTitle:@"Quit EDGESCRIBE"
                action:@selector(terminate:)
                keyEquivalent:@"q"];
        [appMenuItem setSubmenu:appMenu];
        [NSApp setMainMenu:menuBar];

        // Run event loop
        [NSApp run];
    }
    return 0;
}
```

---

## Integration in main.cpp

```cpp
// src/cli/main.cpp

#ifdef EDGESCRIBE_HAS_GUI
#include "gui/gui_launcher.h"
#endif

// In the command routing section:
if (command == "gui") {
    int port = 8080;
    // Parse --port flag...

    // Start server in background thread
    ServerConfig config;
    config.port = port;
    config.host = "127.0.0.1";
    // ... set model paths ...

    std::thread server_thread([&]() {
        ApiServer server(config);
        server.Start();  // Blocks until stopped
    });
    server_thread.detach();

    // Wait for server to be ready
    std::this_thread::sleep_for(std::chrono::milliseconds(500));

#ifdef EDGESCRIBE_HAS_GUI
    return LaunchGui(port);  // Blocks on window event loop
#else
    // Fallback: open browser
    #ifdef _WIN32
    ShellExecuteA(nullptr, "open",
        ("http://localhost:" + std::to_string(port)).c_str(),
        nullptr, nullptr, SW_SHOWNORMAL);
    #else
    system(("open http://localhost:" + std::to_string(port)).c_str());
    #endif
    server_thread.join();
    return 0;
#endif
}
```

---

## Installer Changes (Inno Setup)

```iss
; Desktop shortcut — launches GUI mode
[Icons]
Name: "{commondesktop}\EDGESCRIBE"; \
    Filename: "{app}\edgescribe.exe"; \
    Parameters: "gui"; \
    IconFilename: "{app}\edgescribe.ico"; \
    Comment: "EDGESCRIBE — On-Device AI Assistant"

; Start Menu shortcut
Name: "{group}\EDGESCRIBE"; \
    Filename: "{app}\edgescribe.exe"; \
    Parameters: "gui"; \
    IconFilename: "{app}\edgescribe.ico"

; Optional: auto-start on login
Name: "{userstartup}\EDGESCRIBE"; \
    Filename: "{app}\edgescribe.exe"; \
    Parameters: "gui --minimized"; \
    IconFilename: "{app}\edgescribe.ico"
```

---

## Window Features

### Minimum viable (v1.2)

```
✅ Native window with title bar "EDGESCRIBE"
✅ WebView loads localhost:8080
✅ No URL bar, no tabs, no browser chrome
✅ Window resizable, remembers position/size
✅ Desktop icon + Start Menu shortcut
✅ Close window = exit app
```

### Enhanced (v1.3)

```
- System tray icon (minimize to tray instead of closing)
- Window position/size saved to registry/plist
- Custom app icon in taskbar/dock
- Splash screen while server starts
- --minimized flag (start in tray only)
```

---

## Build Configuration

### Conditional compilation

```cmake
# Default: GUI disabled (CLI + server only)
option(EDGESCRIBE_BUILD_GUI "Build with native GUI window" OFF)

# Enable for desktop builds:
# cmake -B build -DEDGESCRIBE_BUILD_GUI=ON ...
```

When `EDGESCRIBE_BUILD_GUI=OFF`:
- `edgescribe gui` falls back to opening the system browser
- No WebView2/WKWebView dependency
- Binary stays small (~2 MB)

When `EDGESCRIBE_BUILD_GUI=ON`:
- `edgescribe gui` opens a native window
- Windows: links WebView2Loader.dll (~1 MB)
- macOS: links WebKit framework (part of OS, 0 MB)

---

## Overhead Comparison

| Approach | Extra download | RAM overhead | Feels like |
|---|---|---|---|
| **Electron** (LM Studio) | +300 MB | +300 MB | Native app |
| **Tauri** (Jan.ai) | +10 MB | +50 MB | Native app |
| **WebView2/WKWebView** (EDGESCRIBE) | +0-1 MB | +30 MB | Native app |
| **Browser tab** (current) | 0 | 0 (uses existing browser) | Website |

WebView2/WKWebView gives the **same UX as Electron/Tauri** at a fraction of the overhead — because the rendering engine is already part of the OS.

---

## Files to Create

| File | Platform | Lines |
|---|---|---|
| `src/gui/gui_launcher.h` | Cross-platform header | ~15 |
| `src/gui/gui_launcher_win.cpp` | Windows (WebView2) | ~80 |
| `src/gui/gui_launcher_mac.mm` | macOS (WKWebView) | ~60 |

## Files to Modify

| File | Changes |
|---|---|
| `CMakeLists.txt` | Add GUI option, WebView2 SDK, conditional compile |
| `src/cli/main.cpp` | Wire `gui` command to LaunchGui() + server thread |
| `installer/windows/edgescribe.iss` | Desktop shortcut targets `gui` instead of `serve --open` |

---

## Roadmap

```
v1.0:  edgescribe serve → user opens browser manually (or --open flag)
v1.2:  edgescribe gui → native WebView2/WKWebView window
v1.3:  System tray integration, minimize to tray, auto-start on login
```
