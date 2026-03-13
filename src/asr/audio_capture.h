// EDGESCRIBE — Cross-platform audio capture via miniaudio

#pragma once

#include <atomic>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace EDGESCRIBE {

// Callback: receives float32 PCM samples (16kHz, mono)
using AudioCallback = std::function<void(const float* samples, size_t count)>;

class AudioCapture {
 public:
  AudioCapture();
  ~AudioCapture();

  // Start capturing audio from the default microphone
  void Start(AudioCallback callback);

  // Stop capturing
  void Stop();

  // Check if currently capturing
  bool IsCapturing() const { return capturing_.load(); }

  // List available audio input devices
  static std::vector<std::string> ListDevices();

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
  std::atomic<bool> capturing_{false};
  AudioCallback callback_;

  // miniaudio callback (static, forwards to instance)
  static void DataCallback(void* device, void* output, const void* input,
                           unsigned int frame_count);
};

}  // namespace EDGESCRIBE
