// EDGESCRIBE — REST API Server
// Exposes all EDGESCRIBE engines via a local HTTP API
// Usage: EDGESCRIBE serve --port 8080

#pragma once

#include <memory>
#include <string>

namespace EDGESCRIBE::server {

struct ServerConfig {
  int port = 8080;
  std::string host = "127.0.0.1";
  std::string device = "cpu";      // Execution provider: cpu, cuda, dml, vulkan, etc.
  std::string asr_model;
  std::string vlm_model;
  std::string tts_model;
};

class ApiServer {
 public:
  explicit ApiServer(const ServerConfig& config);
  ~ApiServer();

  // Start the server (blocks until stopped)
  void Start();

  // Stop the server gracefully
  void Stop();

  // Check if running
  bool IsRunning() const;

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace EDGESCRIBE::server
