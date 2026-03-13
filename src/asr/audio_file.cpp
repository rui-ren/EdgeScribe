// EDGESCRIBE — WAV file loader implementation
// Uses miniaudio's built-in WAV decoder for cross-platform support

#include "asr/audio_file.h"
#include "miniaudio.h"
#include <stdexcept>
#include <cstring>

namespace EDGESCRIBE {

AudioData LoadWavFile(const std::string& path) {
  // Use miniaudio's decoder to load and convert any WAV to 16kHz float32 mono
  ma_decoder_config config = ma_decoder_config_init(ma_format_f32, 1, 16000);

  ma_decoder decoder;
  ma_result result = ma_decoder_init_file(path.c_str(), &config, &decoder);
  if (result != MA_SUCCESS) {
    throw std::runtime_error("Failed to open audio file: " + path +
                             " (error: " + std::to_string(result) + ")");
  }

  // Get total frame count
  ma_uint64 total_frames;
  result = ma_decoder_get_length_in_pcm_frames(&decoder, &total_frames);
  if (result != MA_SUCCESS || total_frames == 0) {
    // If we can't get length, read in chunks
    total_frames = 0;
  }

  AudioData audio;
  audio.sample_rate = 16000;
  audio.channels = 1;

  if (total_frames > 0) {
    audio.samples.resize(static_cast<size_t>(total_frames));
    ma_uint64 frames_read;
    result = ma_decoder_read_pcm_frames(&decoder, audio.samples.data(),
                                        total_frames, &frames_read);
    audio.samples.resize(static_cast<size_t>(frames_read));
  } else {
    // Read in chunks for streams/pipes
    constexpr size_t kChunkFrames = 16000;  // 1 second chunks
    std::vector<float> chunk(kChunkFrames);
    while (true) {
      ma_uint64 frames_read;
      result = ma_decoder_read_pcm_frames(&decoder, chunk.data(),
                                          kChunkFrames, &frames_read);
      if (frames_read == 0) break;
      audio.samples.insert(audio.samples.end(),
                           chunk.begin(),
                           chunk.begin() + static_cast<size_t>(frames_read));
    }
  }

  ma_decoder_uninit(&decoder);

  audio.duration_seconds = static_cast<double>(audio.samples.size()) / audio.sample_rate;

  if (audio.samples.empty()) {
    throw std::runtime_error("Audio file is empty or unreadable: " + path);
  }

  return audio;
}

}  // namespace EDGESCRIBE
