// EDGESCRIBE — Native GUI Launcher
// Opens the web UI in a native desktop window using webview.
// The API server runs in a background thread; the webview event loop
// runs on the main thread.  Closing the window stops the server.

#pragma once

#include <string>
#include "server/api_server.h"

namespace EDGESCRIBE::gui {

struct GuiConfig {
  server::ServerConfig server_config;
  int width  = 1280;
  int height = 800;
  std::string title = "EDGESCRIBE";
};

// Start the API server in a background thread and open a native window
// pointing at http://host:port.  Blocks until the user closes the window,
// then gracefully stops the server before returning.
void Launch(const GuiConfig& config);

}  // namespace EDGESCRIBE::gui
