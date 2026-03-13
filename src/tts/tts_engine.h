// EDGESCRIBE — TTS Engine
// Wraps Kokoro ONNX model via ONNX Runtime C++ API for text-to-speech

#pragma once

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace EDGESCRIBE::tts {

// Audio output format
struct AudioOutput {
  std::vector<float> samples;  // Float32 PCM samples
  int sample_rate;             // 24000 Hz for Kokoro
};

// Callback for progress
using ProgressCallback = std::function<void(float progress)>;

class TtsEngine {
 public:
  // model_path should contain: model.onnx, voices.bin, config.json
  explicit TtsEngine(const std::string& model_path);
  ~TtsEngine();

  // Synthesize text to audio
  AudioOutput Synthesize(const std::string& text,
                         const std::string& voice = "af_heart",
                         float speed = 1.0f);

  // Synthesize and save to WAV file
  void SynthesizeToFile(const std::string& text,
                        const std::string& output_path,
                        const std::string& voice = "af_heart",
                        float speed = 1.0f);

  // Synthesize and play through speakers
  void Speak(const std::string& text,
             const std::string& voice = "af_heart",
             float speed = 1.0f);

  // List available voices
  std::vector<std::string> ListVoices() const;

  // Write PCM samples to WAV file (public for server use)
  static void WriteWav(const std::string& path,
                       const float* samples, size_t count,
                       int sample_rate);

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace EDGESCRIBE::tts
