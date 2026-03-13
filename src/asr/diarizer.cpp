// EDGESCRIBE — Speaker Diarizer Implementation
// Uses ECAPA-TDNN ONNX model via ONNX Runtime C++ API for speaker embeddings
//
// ECAPA-TDNN model I/O:
//   Input:  audio_signal (float32[1, T])  — raw PCM samples (16kHz mono)
//           audio_length (int64[1])        — number of samples
//   Output: embeddings   (float32[1, D])  — speaker embedding vector (D=192)
//
// The model produces a fixed-size embedding regardless of audio length.
// Same speaker → high cosine similarity (>0.65), different speaker → low (<0.4)

#include "asr/diarizer.h"
#include "onnxruntime_cxx_api.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <numeric>
#include <stdexcept>

namespace fs = std::filesystem;

namespace EDGESCRIBE::asr {

struct Diarizer::Impl {
  Ort::Env env{ORT_LOGGING_LEVEL_WARNING, "EDGESCRIBE-diarizer"};
  Ort::SessionOptions session_options;
  std::unique_ptr<Ort::Session> session;
};

Diarizer::Diarizer(const std::string& model_path)
    : impl_(std::make_unique<Impl>()) {

  impl_->session_options.SetGraphOptimizationLevel(
      GraphOptimizationLevel::ORT_ENABLE_ALL);
  impl_->session_options.SetIntraOpNumThreads(0);

  // Find the speaker embedding model file
  fs::path model_file;
  if (fs::is_regular_file(model_path)) {
    model_file = model_path;
  } else {
    // Try common filenames
    for (const char* name : {"speaker_model.onnx", "ecapa_tdnn.onnx",
                              "model.onnx", "speaker.onnx"}) {
      auto candidate = fs::path(model_path) / name;
      if (fs::exists(candidate)) {
        model_file = candidate;
        break;
      }
    }
  }

  if (model_file.empty() || !fs::exists(model_file)) {
    throw std::runtime_error(
        "Cannot find speaker embedding model in: " + model_path +
        "\nExpected: speaker_model.onnx or ecapa_tdnn.onnx");
  }

#ifdef _WIN32
  std::wstring wide_path = model_file.wstring();
  impl_->session = std::make_unique<Ort::Session>(
      impl_->env, wide_path.c_str(), impl_->session_options);
#else
  impl_->session = std::make_unique<Ort::Session>(
      impl_->env, model_file.c_str(), impl_->session_options);
#endif

  // Detect embedding dimension from model output shape
  auto output_info = impl_->session->GetOutputTypeInfo(0);
  auto tensor_info = output_info.GetTensorTypeAndShapeInfo();
  auto shape = tensor_info.GetShape();
  if (shape.size() >= 2 && shape[1] > 0) {
    embedding_dim_ = static_cast<size_t>(shape[1]);
  }

  std::cout << "Speaker diarizer loaded (embedding dim: "
            << embedding_dim_ << ")" << std::endl;
}

Diarizer::~Diarizer() = default;

std::vector<float> Diarizer::GetEmbedding(const float* samples, size_t count) {
  if (count == 0) {
    return std::vector<float>(embedding_dim_, 0.0f);
  }

  Ort::MemoryInfo mem_info = Ort::MemoryInfo::CreateCpu(
      OrtArenaAllocator, OrtMemTypeDefault);

  // Prepare inputs — try to match common ECAPA-TDNN input formats
  Ort::AllocatorWithDefaultOptions allocator;
  size_t num_inputs = impl_->session->GetInputCount();

  std::vector<Ort::AllocatedStringPtr> input_name_ptrs;
  std::vector<const char*> input_names;
  for (size_t i = 0; i < num_inputs; i++) {
    input_name_ptrs.push_back(
        impl_->session->GetInputNameAllocated(i, allocator));
    input_names.push_back(input_name_ptrs.back().get());
  }

  size_t num_outputs = impl_->session->GetOutputCount();
  std::vector<Ort::AllocatedStringPtr> output_name_ptrs;
  std::vector<const char*> output_names;
  for (size_t i = 0; i < num_outputs; i++) {
    output_name_ptrs.push_back(
        impl_->session->GetOutputNameAllocated(i, allocator));
    output_names.push_back(output_name_ptrs.back().get());
  }

  // Build input tensors
  std::vector<Ort::Value> input_tensors;

  // Audio signal: [1, T]
  std::array<int64_t, 2> audio_shape = {1, static_cast<int64_t>(count)};
  // Need a non-const copy for CreateTensor
  std::vector<float> audio_copy(samples, samples + count);
  input_tensors.push_back(Ort::Value::CreateTensor<float>(
      mem_info, audio_copy.data(), count,
      audio_shape.data(), audio_shape.size()));

  // If model expects a second input (audio_length), provide it
  int64_t audio_len = static_cast<int64_t>(count);
  if (num_inputs > 1) {
    std::array<int64_t, 1> len_shape = {1};
    input_tensors.push_back(Ort::Value::CreateTensor<int64_t>(
        mem_info, &audio_len, 1,
        len_shape.data(), len_shape.size()));
  }

  // Run inference
  auto outputs = impl_->session->Run(
      Ort::RunOptions{nullptr},
      input_names.data(), input_tensors.data(), input_tensors.size(),
      output_names.data(), output_names.size());

  // Extract embedding
  std::vector<float> embedding(embedding_dim_, 0.0f);
  if (!outputs.empty() && outputs[0].IsTensor()) {
    const float* data = outputs[0].GetTensorData<float>();
    auto tensor_info = outputs[0].GetTensorTypeAndShapeInfo();
    auto shape = tensor_info.GetShape();

    size_t total = 1;
    for (auto dim : shape) {
      if (dim > 0) total *= static_cast<size_t>(dim);
    }

    size_t copy_size = std::min(total, embedding_dim_);
    std::copy(data, data + copy_size, embedding.begin());
  }

  // L2 normalize the embedding
  float norm = 0.0f;
  for (float v : embedding) norm += v * v;
  norm = std::sqrt(norm);
  if (norm > 1e-8f) {
    for (float& v : embedding) v /= norm;
  }

  return embedding;
}

SpeakerSegment Diarizer::IdentifySpeaker(const float* samples, size_t count) {
  auto embedding = GetEmbedding(samples, count);
  return IdentifySpeaker(embedding);
}

SpeakerSegment Diarizer::IdentifySpeaker(const std::vector<float>& embedding) {
  // Find the most similar existing speaker
  int best_id = -1;
  float best_sim = -1.0f;

  for (const auto& speaker : speakers_) {
    float sim = CosineSimilarity(embedding, speaker.centroid);
    if (sim > best_sim) {
      best_sim = sim;
      best_id = speaker.id;
    }
  }

  // If similarity is above threshold, assign to existing speaker
  if (best_id >= 0 && best_sim >= threshold_) {
    // Update centroid with running average
    for (auto& speaker : speakers_) {
      if (speaker.id == best_id) {
        UpdateCentroid(speaker, embedding);
        return {best_id, speaker.label, best_sim};
      }
    }
  }

  // New speaker detected
  int new_id = static_cast<int>(speakers_.size());
  SpeakerProfile profile;
  profile.id = new_id;
  profile.label = "Speaker " + std::to_string(new_id + 1);
  profile.centroid = embedding;
  profile.sample_count = 1;
  speakers_.push_back(std::move(profile));

  return {new_id, speakers_.back().label,
          best_sim > 0 ? best_sim : 1.0f};
}

void Diarizer::SetSpeakerLabel(int speaker_id, const std::string& label) {
  for (auto& speaker : speakers_) {
    if (speaker.id == speaker_id) {
      speaker.label = label;
      return;
    }
  }
}

int Diarizer::GetSpeakerCount() const {
  return static_cast<int>(speakers_.size());
}

void Diarizer::Reset() {
  speakers_.clear();
}

float Diarizer::CosineSimilarity(const std::vector<float>& a,
                                 const std::vector<float>& b) {
  if (a.size() != b.size() || a.empty()) return 0.0f;

  float dot = 0.0f, norm_a = 0.0f, norm_b = 0.0f;
  for (size_t i = 0; i < a.size(); i++) {
    dot += a[i] * b[i];
    norm_a += a[i] * a[i];
    norm_b += b[i] * b[i];
  }

  float denom = std::sqrt(norm_a) * std::sqrt(norm_b);
  return denom > 1e-8f ? dot / denom : 0.0f;
}

void Diarizer::UpdateCentroid(SpeakerProfile& profile,
                              const std::vector<float>& new_embedding) {
  // Running average: centroid = (centroid * n + new) / (n + 1)
  float n = static_cast<float>(profile.sample_count);
  for (size_t i = 0; i < profile.centroid.size() && i < new_embedding.size(); i++) {
    profile.centroid[i] = (profile.centroid[i] * n + new_embedding[i]) / (n + 1.0f);
  }
  profile.sample_count++;

  // Re-normalize centroid
  float norm = 0.0f;
  for (float v : profile.centroid) norm += v * v;
  norm = std::sqrt(norm);
  if (norm > 1e-8f) {
    for (float& v : profile.centroid) v /= norm;
  }
}

}  // namespace EDGESCRIBE::asr
