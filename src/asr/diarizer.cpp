// EDGESCRIBE — Speaker Diarizer Implementation
//
// Uses ECAPA-TDNN (ONNX) for speaker embeddings + agglomerative clustering.
//
// Pipeline:
//   1. Split audio into overlapping windows
//   2. Filter out silence (energy-based VAD)
//   3. Compute 80-dim fbank features per window
//   4. Run ECAPA-TDNN → 192-dim embedding per window
//   5. Agglomerative clustering (cosine similarity)
//   6. Map speaker labels back to TimestampedWord segments

#include "asr/diarizer.h"
#include "asr/transcriber.h"

#if __has_include("onnxruntime_cxx_api.h")
#include "onnxruntime_cxx_api.h"
#elif __has_include("onnxruntime/core/session/onnxruntime_cxx_api.h")
#include "onnxruntime/core/session/onnxruntime_cxx_api.h"
#else
#error "Cannot find ONNX Runtime C++ API header (onnxruntime_cxx_api.h)"
#endif

#include <algorithm>
#include <cmath>
#include <iostream>
#include <numeric>
#include <sstream>
#include <stdexcept>
#include <thread>
#include <vector>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace EDGESCRIBE {

// ── Audio Feature Extraction ────────────────────────────────────────────────

// Compute RMS energy of a segment
static float ComputeRmsEnergy(const float* samples, size_t count) {
  if (count == 0) return 0.0f;
  double sum_sq = 0.0;
  for (size_t i = 0; i < count; i++) {
    sum_sq += static_cast<double>(samples[i]) * samples[i];
  }
  return static_cast<float>(std::sqrt(sum_sq / count));
}

// Hann window
static void ApplyHannWindow(float* data, size_t n) {
  for (size_t i = 0; i < n; i++) {
    double w = 0.5 * (1.0 - std::cos(2.0 * M_PI * i / (n - 1)));
    data[i] *= static_cast<float>(w);
  }
}

// In-place Cooley-Tukey radix-2 FFT (complex, length must be power of 2)
static void FFT(std::vector<float>& real, std::vector<float>& imag) {
  size_t n = real.size();
  if (n <= 1) return;

  // Bit-reversal permutation
  for (size_t i = 1, j = 0; i < n; i++) {
    size_t bit = n >> 1;
    for (; j & bit; bit >>= 1) {
      j ^= bit;
    }
    j ^= bit;
    if (i < j) {
      std::swap(real[i], real[j]);
      std::swap(imag[i], imag[j]);
    }
  }

  // Butterfly stages
  for (size_t len = 2; len <= n; len <<= 1) {
    double angle = -2.0 * M_PI / len;
    float w_real = static_cast<float>(std::cos(angle));
    float w_imag = static_cast<float>(std::sin(angle));
    for (size_t i = 0; i < n; i += len) {
      float wr = 1.0f, wi = 0.0f;
      for (size_t j = 0; j < len / 2; j++) {
        float tr = wr * real[i + j + len / 2] - wi * imag[i + j + len / 2];
        float ti = wr * imag[i + j + len / 2] + wi * real[i + j + len / 2];
        real[i + j + len / 2] = real[i + j] - tr;
        imag[i + j + len / 2] = imag[i + j] - ti;
        real[i + j] += tr;
        imag[i + j] += ti;
        float new_wr = wr * w_real - wi * w_imag;
        wi = wr * w_imag + wi * w_real;
        wr = new_wr;
      }
    }
  }
}

// Compute power spectrogram for one frame
static std::vector<float> ComputePowerSpectrum(const float* frame, size_t frame_len,
                                                size_t fft_size) {
  std::vector<float> real(fft_size, 0.0f);
  std::vector<float> imag(fft_size, 0.0f);

  // Copy frame and apply window
  for (size_t i = 0; i < frame_len && i < fft_size; i++) {
    real[i] = frame[i];
  }
  ApplyHannWindow(real.data(), frame_len);

  FFT(real, imag);

  // Power spectrum (only positive frequencies)
  size_t n_freqs = fft_size / 2 + 1;
  std::vector<float> power(n_freqs);
  for (size_t i = 0; i < n_freqs; i++) {
    power[i] = real[i] * real[i] + imag[i] * imag[i];
  }
  return power;
}

// Build mel filterbank matrix
static std::vector<std::vector<float>> BuildMelFilterbank(
    int n_mels, int n_fft, int sample_rate) {
  auto hz_to_mel = [](float hz) -> float {
    return 2595.0f * std::log10(1.0f + hz / 700.0f);
  };
  auto mel_to_hz = [](float mel) -> float {
    return 700.0f * (std::pow(10.0f, mel / 2595.0f) - 1.0f);
  };

  float mel_low = hz_to_mel(0.0f);
  float mel_high = hz_to_mel(static_cast<float>(sample_rate) / 2.0f);

  // Equally spaced mel points
  std::vector<float> mel_points(n_mels + 2);
  for (int i = 0; i < n_mels + 2; i++) {
    mel_points[i] = mel_low + (mel_high - mel_low) * i / (n_mels + 1);
  }

  // Convert back to Hz and then to FFT bin indices
  int n_freqs = n_fft / 2 + 1;
  std::vector<int> bin_indices(n_mels + 2);
  for (int i = 0; i < n_mels + 2; i++) {
    float hz = mel_to_hz(mel_points[i]);
    bin_indices[i] = static_cast<int>(std::floor((n_fft + 1) * hz / sample_rate));
  }

  // Build triangular filters
  std::vector<std::vector<float>> filterbank(n_mels, std::vector<float>(n_freqs, 0.0f));
  for (int m = 0; m < n_mels; m++) {
    int f_left = bin_indices[m];
    int f_center = bin_indices[m + 1];
    int f_right = bin_indices[m + 2];

    for (int k = f_left; k < f_center && k < n_freqs; k++) {
      if (f_center != f_left) {
        filterbank[m][k] = static_cast<float>(k - f_left) / (f_center - f_left);
      }
    }
    for (int k = f_center; k < f_right && k < n_freqs; k++) {
      if (f_right != f_center) {
        filterbank[m][k] = static_cast<float>(f_right - k) / (f_right - f_center);
      }
    }
  }

  return filterbank;
}

// Compute 80-dim log mel fbank features for an audio segment
// Returns shape: [num_frames, 80]
static std::vector<std::vector<float>> ComputeFbank(
    const float* audio, size_t num_samples, int sample_rate,
    int n_mels = 80, int frame_length_ms = 25, int frame_shift_ms = 10) {

  int frame_length = sample_rate * frame_length_ms / 1000;
  int frame_shift = sample_rate * frame_shift_ms / 1000;

  // FFT size = next power of 2 >= frame_length
  int fft_size = 1;
  while (fft_size < frame_length) fft_size <<= 1;

  auto filterbank = BuildMelFilterbank(n_mels, fft_size, sample_rate);

  std::vector<std::vector<float>> features;

  for (size_t start = 0; start + frame_length <= num_samples; start += frame_shift) {
    auto power = ComputePowerSpectrum(audio + start, frame_length, fft_size);

    // Apply mel filterbank
    std::vector<float> mel_energies(n_mels);
    for (int m = 0; m < n_mels; m++) {
      float energy = 0.0f;
      for (size_t k = 0; k < power.size() && k < filterbank[m].size(); k++) {
        energy += filterbank[m][k] * power[k];
      }
      // Log with floor to avoid log(0)
      mel_energies[m] = std::log(std::max(energy, 1e-10f));
    }

    features.push_back(std::move(mel_energies));
  }

  return features;
}

// ── Clustering ──────────────────────────────────────────────────────────────

// Cosine similarity between two vectors
static float CosineSimilarity(const std::vector<float>& a,
                               const std::vector<float>& b) {
  if (a.size() != b.size() || a.empty()) return 0.0f;

  float dot = 0.0f, norm_a = 0.0f, norm_b = 0.0f;
  for (size_t i = 0; i < a.size(); i++) {
    dot += a[i] * b[i];
    norm_a += a[i] * a[i];
    norm_b += b[i] * b[i];
  }

  float denom = std::sqrt(norm_a) * std::sqrt(norm_b);
  return (denom > 1e-8f) ? (dot / denom) : 0.0f;
}

// Agglomerative hierarchical clustering (average linkage, cosine similarity)
// Returns cluster assignment for each embedding index
static std::vector<int> AgglomerativeClustering(
    const std::vector<std::vector<float>>& embeddings,
    float similarity_threshold,
    int max_clusters) {

  int n = static_cast<int>(embeddings.size());
  if (n == 0) return {};
  if (n == 1) return {0};

  // Each embedding starts in its own cluster
  std::vector<int> labels(n);
  std::iota(labels.begin(), labels.end(), 0);

  // Cluster centroids (average of member embeddings)
  int embed_dim = static_cast<int>(embeddings[0].size());
  std::vector<std::vector<float>> centroids(embeddings.begin(), embeddings.end());
  std::vector<int> cluster_sizes(n, 1);
  std::vector<bool> active(n, true);

  int num_clusters = n;

  while (num_clusters > 1) {
    // Find most similar pair of active clusters
    float best_sim = -1.0f;
    int best_i = -1, best_j = -1;

    for (int i = 0; i < n; i++) {
      if (!active[i]) continue;
      for (int j = i + 1; j < n; j++) {
        if (!active[j]) continue;
        float sim = CosineSimilarity(centroids[i], centroids[j]);
        if (sim > best_sim) {
          best_sim = sim;
          best_i = i;
          best_j = j;
        }
      }
    }

    if (best_i < 0 || best_sim < similarity_threshold) break;
    if (num_clusters <= max_clusters && best_sim < similarity_threshold) break;

    // Merge cluster j into cluster i (weighted average centroid)
    int total = cluster_sizes[best_i] + cluster_sizes[best_j];
    for (int d = 0; d < embed_dim; d++) {
      centroids[best_i][d] =
          (centroids[best_i][d] * cluster_sizes[best_i] +
           centroids[best_j][d] * cluster_sizes[best_j]) / total;
    }
    cluster_sizes[best_i] = total;
    active[best_j] = false;

    // Relabel all members of cluster j → cluster i
    for (int k = 0; k < n; k++) {
      if (labels[k] == best_j) labels[k] = best_i;
    }

    num_clusters--;
  }

  // Compact labels to 0..K-1
  std::vector<int> label_map(n, -1);
  int next_label = 0;
  for (int i = 0; i < n; i++) {
    if (label_map[labels[i]] < 0) {
      label_map[labels[i]] = next_label++;
    }
  }
  for (int i = 0; i < n; i++) {
    labels[i] = label_map[labels[i]];
  }

  return labels;
}

// ── Diarizer Implementation ─────────────────────────────────────────────────

struct Diarizer::Impl {
  Ort::Env env{ORT_LOGGING_LEVEL_WARNING, "EDGESCRIBE-diarizer"};
  Ort::SessionOptions session_options;
  std::unique_ptr<Ort::Session> session;

  std::string input_name;
  std::string output_name;

  void LoadModel(const std::string& model_path, const std::string& device);

  std::vector<float> ExtractEmbedding(const std::vector<std::vector<float>>& fbank);
};

void Diarizer::Impl::LoadModel(const std::string& model_path,
                                const std::string& device) {
  session_options.SetIntraOpNumThreads(
      static_cast<int>(std::thread::hardware_concurrency()));
  session_options.SetGraphOptimizationLevel(
      GraphOptimizationLevel::ORT_ENABLE_ALL);

  // Device/EP selection
  if (device == "cuda") {
#ifdef USE_CUDA
    OrtCUDAProviderOptions cuda_opts{};
    session_options.AppendExecutionProvider_CUDA(cuda_opts);
#endif
  } else if (device == "dml") {
#ifdef USE_DML
    session_options.DisableMemPattern();
    session_options.AppendExecutionProvider("DML");
#endif
  }

#ifdef _WIN32
  std::wstring wpath(model_path.begin(), model_path.end());
  session = std::make_unique<Ort::Session>(env, wpath.c_str(), session_options);
#else
  session = std::make_unique<Ort::Session>(env, model_path.c_str(), session_options);
#endif

  // Cache input/output names
  Ort::AllocatorWithDefaultOptions allocator;
  input_name = session->GetInputNameAllocated(0, allocator).get();
  output_name = session->GetOutputNameAllocated(0, allocator).get();
}

std::vector<float> Diarizer::Impl::ExtractEmbedding(
    const std::vector<std::vector<float>>& fbank) {
  if (fbank.empty()) return {};

  int num_frames = static_cast<int>(fbank.size());
  int num_mels = static_cast<int>(fbank[0].size());

  // Flatten [num_frames, num_mels] → contiguous buffer
  std::vector<float> input_data(num_frames * num_mels);
  for (int i = 0; i < num_frames; i++) {
    std::copy(fbank[i].begin(), fbank[i].end(),
              input_data.begin() + i * num_mels);
  }

  // Create input tensor: [1, num_frames, num_mels]
  std::array<int64_t, 3> input_shape = {1, num_frames, num_mels};
  auto memory_info = Ort::MemoryInfo::CreateCpu(
      OrtArenaAllocator, OrtMemTypeDefault);
  auto input_tensor = Ort::Value::CreateTensor<float>(
      memory_info, input_data.data(), input_data.size(),
      input_shape.data(), input_shape.size());

  const char* input_names[] = {input_name.c_str()};
  const char* output_names[] = {output_name.c_str()};

  auto outputs = session->Run(
      Ort::RunOptions{nullptr},
      input_names, &input_tensor, 1,
      output_names, 1);

  // Extract embedding vector
  auto& output_tensor = outputs[0];
  auto type_info = output_tensor.GetTensorTypeAndShapeInfo();
  size_t embed_size = type_info.GetElementCount();

  const float* embed_data = output_tensor.GetTensorData<float>();
  std::vector<float> embedding(embed_data, embed_data + embed_size);

  // L2 normalize
  float norm = 0.0f;
  for (float v : embedding) norm += v * v;
  norm = std::sqrt(norm);
  if (norm > 1e-8f) {
    for (float& v : embedding) v /= norm;
  }

  return embedding;
}

// ── Public API ──────────────────────────────────────────────────────────────

Diarizer::Diarizer(const std::string& model_path, const std::string& device)
    : impl_(std::make_unique<Impl>()) {
  impl_->LoadModel(model_path, device);
}

Diarizer::~Diarizer() = default;

std::vector<SpeakerSegment> Diarizer::DiarizeAudio(
    const float* audio_samples,
    size_t sample_count,
    int sample_rate,
    const DiarizationConfig& config) {

  if (sample_count == 0) return {};

  int seg_samples = static_cast<int>(config.segment_duration * sample_rate);
  int hop_samples = static_cast<int>(
      (config.segment_duration - config.segment_overlap) * sample_rate);
  if (hop_samples <= 0) hop_samples = seg_samples / 2;

  // Segment audio into overlapping windows
  struct WindowInfo {
    size_t start_sample;
    size_t end_sample;
  };
  std::vector<WindowInfo> windows;
  std::vector<std::vector<float>> embeddings;

  for (size_t start = 0; start + seg_samples <= sample_count;
       start += hop_samples) {
    size_t end = start + seg_samples;

    // Energy-based VAD: skip silence
    float rms = ComputeRmsEnergy(audio_samples + start, seg_samples);
    if (rms < config.vad_energy_threshold) continue;

    // Compute fbank features for this window
    auto fbank = ComputeFbank(audio_samples + start, seg_samples, sample_rate);
    if (fbank.empty()) continue;

    // Extract speaker embedding
    auto embedding = impl_->ExtractEmbedding(fbank);
    if (embedding.empty()) continue;

    windows.push_back({start, end});
    embeddings.push_back(std::move(embedding));
  }

  if (embeddings.empty()) return {};

  // Cluster embeddings
  auto labels = AgglomerativeClustering(
      embeddings, config.similarity_threshold, config.max_speakers);

  // Build speaker segments from clustered windows
  std::vector<SpeakerSegment> segments;
  for (size_t i = 0; i < windows.size(); i++) {
    double start_time = static_cast<double>(windows[i].start_sample) / sample_rate;
    double end_time = static_cast<double>(windows[i].end_sample) / sample_rate;
    int spk = labels[i];

    // Merge with previous segment if same speaker and contiguous
    if (!segments.empty() && segments.back().speaker_id == spk &&
        std::abs(segments.back().end_time - start_time) < config.segment_duration) {
      segments.back().end_time = end_time;
    } else {
      SpeakerSegment seg;
      seg.speaker_id = spk;
      seg.speaker_label = "Speaker " + std::to_string(spk + 1);
      seg.start_time = start_time;
      seg.end_time = end_time;
      segments.push_back(std::move(seg));
    }
  }

  return segments;
}

std::vector<SpeakerSegment> Diarizer::Diarize(
    const float* audio_samples,
    size_t sample_count,
    int sample_rate,
    const std::vector<TimestampedWord>& words,
    const DiarizationConfig& config) {

  // Get raw speaker segments from audio
  auto raw_segments = DiarizeAudio(
      audio_samples, sample_count, sample_rate, config);

  if (raw_segments.empty() || words.empty()) return raw_segments;

  // Build merged speaker segments with text
  std::vector<SpeakerSegment> result;

  for (const auto& word : words) {
    // Determine speaker for this word
    int speaker = 0;
    double best_overlap = 0.0;

    for (const auto& seg : raw_segments) {
      double overlap_start = std::max(word.start_time, seg.start_time);
      double overlap_end = std::min(word.end_time, seg.end_time);
      double overlap = std::max(0.0, overlap_end - overlap_start);

      if (overlap > best_overlap) {
        best_overlap = overlap;
        speaker = seg.speaker_id;
      }
    }

    if (best_overlap <= 0.0) {
      double word_mid = (word.start_time + word.end_time) / 2.0;
      double best_dist = 1e9;
      for (const auto& seg : raw_segments) {
        double seg_mid = (seg.start_time + seg.end_time) / 2.0;
        double dist = std::abs(word_mid - seg_mid);
        if (dist < best_dist) {
          best_dist = dist;
          speaker = seg.speaker_id;
        }
      }
    }

    // Merge consecutive same-speaker words
    if (!result.empty() && result.back().speaker_id == speaker) {
      result.back().end_time = word.end_time;
      result.back().text += " " + word.text;
    } else {
      SpeakerSegment seg;
      seg.speaker_id = speaker;
      seg.speaker_label = "Speaker " + std::to_string(speaker + 1);
      seg.start_time = word.start_time;
      seg.end_time = word.end_time;
      seg.text = word.text;
      result.push_back(std::move(seg));
    }
  }

  return result;
}

// ── Formatting ──────────────────────────────────────────────────────────────

std::string FormatDiarizedTranscript(const std::vector<SpeakerSegment>& segments) {
  std::ostringstream ss;
  for (const auto& seg : segments) {
    ss << "[" << seg.speaker_label << "] " << seg.text << "\n";
  }
  return ss.str();
}

}  // namespace EDGESCRIBE
