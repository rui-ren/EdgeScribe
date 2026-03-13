// EDGESCRIBE — Speaker Diarizer
// Identifies WHO is speaking using ONNX speaker embedding model (ECAPA-TDNN)
//
// Pipeline (assumes VAD is handled by onnxruntime-genai):
//   Audio segment → Speaker Embedding ONNX → 192-dim vector → cosine similarity → speaker ID
//
// Does NOT include VAD — that will come from onnxruntime-genai's Silero-VAD integration.

#pragma once

#include <memory>
#include <string>
#include <vector>

namespace EDGESCRIBE::asr {

struct SpeakerSegment {
  int speaker_id;           // 0-based speaker index
  std::string speaker_label; // "Speaker 1", "Speaker 2", or custom name
  float confidence;          // Cosine similarity to assigned speaker cluster
};

class Diarizer {
 public:
  // model_path: directory containing the ECAPA-TDNN ONNX speaker embedding model
  explicit Diarizer(const std::string& model_path);
  ~Diarizer();

  // Get speaker embedding vector for an audio segment
  // Input: float32 PCM samples, 16kHz mono
  // Output: embedding vector (typically 192 dimensions)
  std::vector<float> GetEmbedding(const float* samples, size_t count);

  // Identify which speaker this audio segment belongs to
  // Automatically creates new speaker IDs when a new voice is detected
  SpeakerSegment IdentifySpeaker(const float* samples, size_t count);

  // Identify speaker from a pre-computed embedding
  SpeakerSegment IdentifySpeaker(const std::vector<float>& embedding);

  // Set a custom label for a speaker ID
  void SetSpeakerLabel(int speaker_id, const std::string& label);

  // Get the number of distinct speakers detected so far
  int GetSpeakerCount() const;

  // Reset all speaker state (for a new conversation)
  void Reset();

  // Get the similarity threshold for considering two embeddings as the same speaker
  float GetThreshold() const { return threshold_; }
  void SetThreshold(float threshold) { threshold_ = threshold; }

  // Get embedding dimension
  size_t GetEmbeddingDim() const { return embedding_dim_; }

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;

  // Speaker profiles: centroid embedding per known speaker
  struct SpeakerProfile {
    int id;
    std::string label;
    std::vector<float> centroid;  // Average embedding
    int sample_count;             // Number of segments contributing to centroid
  };

  std::vector<SpeakerProfile> speakers_;
  float threshold_ = 0.65f;  // Cosine similarity threshold for same speaker
  size_t embedding_dim_ = 192;

  // Cosine similarity between two embedding vectors
  static float CosineSimilarity(const std::vector<float>& a,
                                const std::vector<float>& b);

  // Update speaker centroid with new embedding (running average)
  static void UpdateCentroid(SpeakerProfile& profile,
                             const std::vector<float>& new_embedding);
};

}  // namespace EDGESCRIBE::asr
