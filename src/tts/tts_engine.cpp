// EDGESCRIBE — TTS Engine Implementation
// Uses ONNX Runtime C++ API to run the Kokoro TTS model
// Uses espeak-ng for G2P (grapheme-to-phoneme) conversion
//
// Pipeline: text → espeak-ng → IPA phonemes → Kokoro token IDs → ONNX model → audio
//
// Kokoro model I/O:
//   Input:  tokens (int64[1,N])  — phoneme token IDs
//           style  (float32[1,D]) — voice embedding vector
//           speed  (float32[1])   — speaking speed multiplier
//   Output: audio  (float32[1,T]) — PCM waveform at 24kHz

#include "tts/tts_engine.h"
#include "tts/phonemizer.h"
#include "tts/kokoro_vocab.h"
#include "miniaudio.h"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <thread>
#include <unordered_map>

// ONNX Runtime C++ API
#include "onnxruntime_cxx_api.h"

namespace fs = std::filesystem;

namespace EDGESCRIBE::tts {

// ── ONNX Runtime Session ────────────────────────────────────────────────────
struct TtsEngine::Impl {
  Ort::Env env{ORT_LOGGING_LEVEL_WARNING, "EDGESCRIBE-tts"};
  Ort::SessionOptions session_options;
  std::unique_ptr<Ort::Session> session;

  // Phonemizer (espeak-ng or fallback)
  Phonemizer phonemizer;

  // Voice embeddings: voice_name → float vector
  std::unordered_map<std::string, std::vector<float>> voices;
  size_t style_dim = 256;  // Voice embedding dimension

  std::string model_dir;

  void LoadVoices(const std::string& voices_path);
  void LoadVoicesFromDir(const std::string& voices_dir);
};

void TtsEngine::Impl::LoadVoices(const std::string& voices_path) {
  // Try to load voice embeddings from voices.bin
  // Format: Each voice is a named block with dimension D float32 values
  // For now, if the file doesn't exist, create a default neutral voice

  if (fs::exists(voices_path)) {
    std::ifstream ifs(voices_path, std::ios::binary);
    if (ifs.is_open()) {
      // Read binary voice file
      // Simple format: count (int32), then for each voice:
      //   name_len (int32), name (char[]), dim (int32), data (float[dim])
      int32_t count = 0;
      ifs.read(reinterpret_cast<char*>(&count), sizeof(count));

      for (int32_t i = 0; i < count && ifs.good(); i++) {
        int32_t name_len = 0;
        ifs.read(reinterpret_cast<char*>(&name_len), sizeof(name_len));

        std::string name(name_len, '\0');
        ifs.read(name.data(), name_len);

        int32_t dim = 0;
        ifs.read(reinterpret_cast<char*>(&dim), sizeof(dim));
        style_dim = static_cast<size_t>(dim);

        std::vector<float> data(dim);
        ifs.read(reinterpret_cast<char*>(data.data()),
                 dim * sizeof(float));

        voices[name] = std::move(data);
      }
    }
  }

  // If no voices loaded, create a default one
  if (voices.empty()) {
    std::vector<float> default_voice(style_dim, 0.0f);
    // Initialize with small random-like values for a neutral voice
    for (size_t i = 0; i < style_dim; i++) {
      default_voice[i] = 0.01f * static_cast<float>(i % 10 - 5);
    }
    voices["default"] = std::move(default_voice);
  }
}

void TtsEngine::Impl::LoadVoicesFromDir(const std::string& voices_dir) {
  // Load individual .bin voice files from a directory
  // Each file is a raw float32 array (style_dim floats)
  for (const auto& entry : fs::directory_iterator(voices_dir)) {
    if (entry.path().extension() == ".bin") {
      std::string name = entry.path().stem().string();
      auto file_size = fs::file_size(entry.path());
      size_t dim = file_size / sizeof(float);

      if (dim > 0) {
        std::ifstream ifs(entry.path(), std::ios::binary);
        std::vector<float> data(dim);
        ifs.read(reinterpret_cast<char*>(data.data()),
                 static_cast<std::streamsize>(file_size));
        if (ifs.good()) {
          style_dim = dim;
          voices[name] = std::move(data);
        }
      }
    }
  }

  if (voices.empty()) {
    std::vector<float> default_voice(style_dim, 0.0f);
    for (size_t i = 0; i < style_dim; i++) {
      default_voice[i] = 0.01f * static_cast<float>(i % 10 - 5);
    }
    voices["default"] = std::move(default_voice);
  }
}

// ── TTS Engine ──────────────────────────────────────────────────────────────
TtsEngine::TtsEngine(const std::string& model_path) : impl_(std::make_unique<Impl>()) {
  impl_->model_dir = model_path;

  // Configure session
  impl_->session_options.SetGraphOptimizationLevel(
      GraphOptimizationLevel::ORT_ENABLE_ALL);
  impl_->session_options.SetIntraOpNumThreads(0);  // Auto

  // Find model file — try multiple layouts
  fs::path model_file = fs::path(model_path) / "model.onnx";
  if (!fs::exists(model_file)) {
    model_file = fs::path(model_path) / "onnx" / "model_fp16.onnx";
  }
  if (!fs::exists(model_file)) {
    model_file = fs::path(model_path) / "onnx" / "model.onnx";
  }
  if (!fs::exists(model_file)) {
    model_file = fs::path(model_path) / "kokoro.onnx";
  }
  if (!fs::exists(model_file)) {
    throw std::runtime_error("Cannot find TTS model in: " + model_path);
  }

  // Create session
#ifdef _WIN32
  std::wstring wide_path = model_file.wstring();
  impl_->session = std::make_unique<Ort::Session>(
      impl_->env, wide_path.c_str(), impl_->session_options);
#else
  impl_->session = std::make_unique<Ort::Session>(
      impl_->env, model_file.c_str(), impl_->session_options);
#endif

  // Load voice embeddings — try voices.bin (old format) or voices/ directory (new format)
  fs::path voices_file = fs::path(model_path) / "voices.bin";
  if (!fs::exists(voices_file)) {
    // New format: individual .bin files in voices/ directory
    fs::path voices_dir = fs::path(model_path) / "voices";
    if (fs::exists(voices_dir) && fs::is_directory(voices_dir)) {
      impl_->LoadVoicesFromDir(voices_dir.string());
    }
  } else {
    impl_->LoadVoices(voices_file.string());
  }

  // Log phonemizer status
  if (impl_->phonemizer.IsAvailable()) {
    std::cout << "G2P: espeak-ng (IPA phonemes)" << std::endl;
  } else {
    std::cout << "G2P: fallback (install espeak-ng for better quality)" << std::endl;
  }

  // Load tokenizer vocab from tokenizer.json
  GetKokoroVocab(model_path);
}

TtsEngine::~TtsEngine() = default;

AudioOutput TtsEngine::Synthesize(const std::string& text,
                                  const std::string& voice,
                                  float speed) {
  if (text.empty()) {
    return {{}, 24000};
  }

  // Step 1: Convert text to IPA phonemes via espeak-ng (or fallback)
  std::string phonemes = impl_->phonemizer.TextToPhonemes(text);

  // Step 2: Convert IPA phonemes to Kokoro token IDs
  auto tokens = PhonemeStringToTokens(phonemes);

  // Get voice embedding
  std::vector<float> style_vec;
  auto it = impl_->voices.find(voice);
  const auto& full_voice = (it != impl_->voices.end())
      ? it->second : impl_->voices.begin()->second;

  // Kokoro v1.0 voice files are shaped (N, 1, 256) where N = max token positions.
  // Index by len(tokens) to get the (1, 256) style vector for this input length.
  // Kokoro v0.x voice files are just 256 floats (single vector).
  constexpr size_t kStyleDim = 256;
  size_t n_tokens = tokens.size();

  if (full_voice.size() == kStyleDim) {
    // Old format: single 256-dim vector
    style_vec = full_voice;
  } else if (full_voice.size() > kStyleDim && full_voice.size() % kStyleDim == 0) {
    // v1.0 format: (N, 256) — select row indexed by token count
    size_t n_positions = full_voice.size() / kStyleDim;
    size_t idx = std::min(n_tokens, n_positions - 1);
    size_t offset = idx * kStyleDim;
    style_vec.assign(full_voice.begin() + static_cast<ptrdiff_t>(offset),
                     full_voice.begin() + static_cast<ptrdiff_t>(offset + kStyleDim));
  } else {
    style_vec = full_voice;
  }

  // Prepare input tensors
  Ort::MemoryInfo mem_info = Ort::MemoryInfo::CreateCpu(
      OrtArenaAllocator, OrtMemTypeDefault);

  // tokens: [1, N]
  std::array<int64_t, 2> tokens_shape = {1, static_cast<int64_t>(tokens.size())};
  Ort::Value tokens_tensor = Ort::Value::CreateTensor<int64_t>(
      mem_info, tokens.data(), tokens.size(),
      tokens_shape.data(), tokens_shape.size());

  // style: [1, D]
  std::array<int64_t, 2> style_shape = {1, static_cast<int64_t>(style_vec.size())};
  Ort::Value style_tensor = Ort::Value::CreateTensor<float>(
      mem_info, style_vec.data(), style_vec.size(),
      style_shape.data(), style_shape.size());

  // speed: [1]
  std::array<int64_t, 1> speed_shape = {1};
  Ort::Value speed_tensor = Ort::Value::CreateTensor<float>(
      mem_info, &speed, 1,
      speed_shape.data(), speed_shape.size());

  // Get input/output names from model
  Ort::AllocatorWithDefaultOptions allocator;
  size_t num_inputs = impl_->session->GetInputCount();
  size_t num_outputs = impl_->session->GetOutputCount();

  std::vector<Ort::AllocatedStringPtr> input_name_ptrs;
  std::vector<const char*> input_names;
  for (size_t i = 0; i < num_inputs; i++) {
    input_name_ptrs.push_back(impl_->session->GetInputNameAllocated(i, allocator));
    input_names.push_back(input_name_ptrs.back().get());
  }

  std::vector<Ort::AllocatedStringPtr> output_name_ptrs;
  std::vector<const char*> output_names;
  for (size_t i = 0; i < num_outputs; i++) {
    output_name_ptrs.push_back(impl_->session->GetOutputNameAllocated(i, allocator));
    output_names.push_back(output_name_ptrs.back().get());
  }

  // Build input array — order depends on actual model input names
  std::vector<Ort::Value> input_tensors;
  for (size_t i = 0; i < num_inputs; i++) {
    std::string name = input_names[i];
    if (name == "tokens" || name == "input_ids") {
      input_tensors.push_back(std::move(tokens_tensor));
    } else if (name == "style" || name == "voice" || name == "ref_s") {
      input_tensors.push_back(std::move(style_tensor));
    } else if (name == "speed") {
      input_tensors.push_back(std::move(speed_tensor));
    } else {
      // Unknown input — provide tokens as fallback
      // Re-create since we may have moved it
      auto fallback = Ort::Value::CreateTensor<int64_t>(
          mem_info, tokens.data(), tokens.size(),
          tokens_shape.data(), tokens_shape.size());
      input_tensors.push_back(std::move(fallback));
    }
  }

  // Run inference
  auto outputs = impl_->session->Run(
      Ort::RunOptions{nullptr},
      input_names.data(), input_tensors.data(), input_tensors.size(),
      output_names.data(), output_names.size());

  // Extract audio output
  AudioOutput result;
  result.sample_rate = 24000;

  if (!outputs.empty() && outputs[0].IsTensor()) {
    auto tensor_info = outputs[0].GetTensorTypeAndShapeInfo();
    auto shape = tensor_info.GetShape();
    size_t total_samples = 1;
    for (auto dim : shape) {
      if (dim > 0) total_samples *= static_cast<size_t>(dim);
    }

    const float* audio_data = outputs[0].GetTensorData<float>();
    result.samples.assign(audio_data, audio_data + total_samples);
  }

  return result;
}

void TtsEngine::SynthesizeToFile(const std::string& text,
                                 const std::string& output_path,
                                 const std::string& voice,
                                 float speed) {
  auto audio = Synthesize(text, voice, speed);
  if (audio.samples.empty()) {
    throw std::runtime_error("Synthesis produced no audio");
  }
  WriteWav(output_path, audio.samples.data(), audio.samples.size(),
           audio.sample_rate);
}

void TtsEngine::Speak(const std::string& text,
                      const std::string& voice,
                      float speed) {
  auto audio = Synthesize(text, voice, speed);
  if (audio.samples.empty()) {
    std::cerr << "Warning: No audio generated" << std::endl;
    return;
  }

  // Play audio using miniaudio
  ma_device_config config = ma_device_config_init(ma_device_type_playback);
  config.playback.format = ma_format_f32;
  config.playback.channels = 1;
  config.sampleRate = static_cast<ma_uint32>(audio.sample_rate);

  struct PlaybackData {
    const float* samples;
    size_t total;
    size_t position;
  } playback = {audio.samples.data(), audio.samples.size(), 0};

  config.pUserData = &playback;
  config.dataCallback = [](ma_device* device, void* output, const void*,
                           ma_uint32 frame_count) {
    auto* data = static_cast<PlaybackData*>(device->pUserData);
    auto* out = static_cast<float*>(output);
    size_t frames_to_copy = std::min(
        static_cast<size_t>(frame_count),
        data->total - data->position);

    if (frames_to_copy > 0) {
      std::memcpy(out, data->samples + data->position,
                  frames_to_copy * sizeof(float));
      data->position += frames_to_copy;
    }

    // Silence remaining
    if (frames_to_copy < frame_count) {
      std::memset(out + frames_to_copy, 0,
                  (frame_count - frames_to_copy) * sizeof(float));
    }
  };

  ma_device device;
  if (ma_device_init(nullptr, &config, &device) != MA_SUCCESS) {
    throw std::runtime_error("Failed to initialize audio playback device");
  }

  if (ma_device_start(&device) != MA_SUCCESS) {
    ma_device_uninit(&device);
    throw std::runtime_error("Failed to start audio playback");
  }

  // Wait for playback to finish
  while (playback.position < playback.total) {
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
  // Small extra delay for audio buffer to flush
  std::this_thread::sleep_for(std::chrono::milliseconds(100));

  ma_device_uninit(&device);
}

std::vector<std::string> TtsEngine::ListVoices() const {
  std::vector<std::string> names;
  for (const auto& [name, _] : impl_->voices) {
    names.push_back(name);
  }
  std::sort(names.begin(), names.end());
  return names;
}

void TtsEngine::WriteWav(const std::string& path,
                         const float* samples, size_t count,
                         int sample_rate) {
  // Convert float32 to int16 for WAV
  std::vector<int16_t> pcm(count);
  for (size_t i = 0; i < count; i++) {
    float s = std::clamp(samples[i], -1.0f, 1.0f);
    pcm[i] = static_cast<int16_t>(s * 32767.0f);
  }

  std::ofstream ofs(path, std::ios::binary);
  if (!ofs.is_open()) {
    throw std::runtime_error("Cannot write to: " + path);
  }

  // WAV header
  uint32_t data_size = static_cast<uint32_t>(count * sizeof(int16_t));
  uint32_t file_size = 36 + data_size;
  uint16_t channels = 1;
  uint16_t bits_per_sample = 16;
  uint32_t byte_rate = sample_rate * channels * bits_per_sample / 8;
  uint16_t block_align = channels * bits_per_sample / 8;

  ofs.write("RIFF", 4);
  ofs.write(reinterpret_cast<const char*>(&file_size), 4);
  ofs.write("WAVE", 4);
  ofs.write("fmt ", 4);
  uint32_t fmt_size = 16;
  ofs.write(reinterpret_cast<const char*>(&fmt_size), 4);
  uint16_t audio_format = 1;  // PCM
  ofs.write(reinterpret_cast<const char*>(&audio_format), 2);
  ofs.write(reinterpret_cast<const char*>(&channels), 2);
  uint32_t sr = static_cast<uint32_t>(sample_rate);
  ofs.write(reinterpret_cast<const char*>(&sr), 4);
  ofs.write(reinterpret_cast<const char*>(&byte_rate), 4);
  ofs.write(reinterpret_cast<const char*>(&block_align), 2);
  ofs.write(reinterpret_cast<const char*>(&bits_per_sample), 2);
  ofs.write("data", 4);
  ofs.write(reinterpret_cast<const char*>(&data_size), 4);
  ofs.write(reinterpret_cast<const char*>(pcm.data()), data_size);
}

}  // namespace EDGESCRIBE::tts
