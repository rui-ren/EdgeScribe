// EDGESCRIBE — WAV file loader

#pragma once

#include <string>
#include <vector>

namespace EDGESCRIBE {

struct AudioData {
  std::vector<float> samples;   // Float32 PCM samples normalized to [-1, 1]
  uint32_t sample_rate;         // Sample rate in Hz
  uint16_t channels;            // Number of channels
  double duration_seconds;      // Duration in seconds
};

// Load a WAV file and return float32 PCM samples (resampled to 16kHz mono)
AudioData LoadWavFile(const std::string& path);

}  // namespace EDGESCRIBE
