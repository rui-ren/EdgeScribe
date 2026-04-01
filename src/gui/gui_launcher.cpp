// EDGESCRIBE — Native GUI Launcher (implementation)

#include "gui/gui_launcher.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#endif

#include "webview/webview.h"

#include <atomic>
#include <chrono>
#include <iostream>
#include <thread>

namespace EDGESCRIBE::gui {

void Launch(const GuiConfig& config) {
  // ── Start the REST API server in a background thread ──
  server::ApiServer api_server(config.server_config);
  std::atomic<bool> server_started{false};

  std::thread server_thread([&]() {
    server_started.store(true);
    api_server.Start();  // blocks until Stop() is called
  });

  // Wait for the listener thread to spin up
  while (!server_started.load()) {
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
  }
  // Give the HTTP listener a moment to bind the port
  std::this_thread::sleep_for(std::chrono::milliseconds(500));

  // ── Build the URL ──
  const std::string url =
      "http://" + config.server_config.host + ":" +
      std::to_string(config.server_config.port);

  std::cout << "EDGESCRIBE is running at " << url << std::endl;

  // ── Create and run the native window ──
  // debug=false in release; set to true for DevTools access
  webview::webview w(false, nullptr);
  w.set_title(config.title);
  w.set_size(config.width, config.height, WEBVIEW_HINT_NONE);
  w.navigate(url);
  w.run();  // blocks until the window is closed

  // ── User closed the window — shut everything down ──
  std::cout << "Window closed. Shutting down..." << std::endl;
  api_server.Stop();

  if (server_thread.joinable()) {
    server_thread.join();
  }
}

}  // namespace EDGESCRIBE::gui
