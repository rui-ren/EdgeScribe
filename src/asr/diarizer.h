// EDGESCRIBE — Speaker Diarizer
// Assigns speaker labels to transcribed segments using ECAPA-TDNN embeddings
// and agglomerative clustering (cosine similarity).
//
// Pipeline:
//   Audio → VAD segmentation → ECAPA-TDNN embeddings → HAC clustering → speaker labels
//
// The ECAPA-TDNN model (~15MB ONNX) produces 192-dim speaker embeddings.
// Clustering groups embeddings by speaker identity without pre-enrollment.

#pragma once

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace EDGESCRIBE {

struct TimestampedWord;  // forward declaration from transcriber.h

// A contiguous segment of speech from a single speaker
struct SpeakerSegment {
  int speaker_id;            // 0-based speaker index
  std::string speaker_label; // "Speaker 1", "Speaker 2", etc.
  double start_time;         // seconds
  double end_time;           // seconds
  std::string text;          // concatenated transcript for this segment
};

struct DiarizationConfig {
  float similarity_threshold = 0.65f; // cosine similarity threshold for same-speaker
  int max_speakers = 8;               // maximum number of speakers to detect
  float segment_duration = 1.5f;      // seconds per embedding window
  float segment_overlap = 0.75f;      // seconds overlap between windows
  float vad_energy_threshold = 0.01f; // RMS energy threshold for speech detection
};

class Diarizer {
 public:
  explicit Diarizer(const std::string& model_path,
                    const std::string& device = "cpu");
  ~Diarizer();

  Diarizer(const Diarizer&) = delete;
  Diarizer& operator=(const Diarizer&) = delete;

  // Run diarization on audio and map results to timestamped words.
  // Returns speaker segments with text from the word list.
  std::vector<SpeakerSegment> Diarize(
      const float* audio_samples,
      size_t sample_count,
      int sample_rate,
      const std::vector<TimestampedWord>& words,
      const DiarizationConfig& config = {});

  // Run diarization on audio only (no word mapping).
  // Returns raw speaker segments with time ranges only.
  std::vector<SpeakerSegment> DiarizeAudio(
      const float* audio_samples,
      size_t sample_count,
      int sample_rate,
      const DiarizationConfig& config = {});

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

// Format diarized output as labeled transcript text
std::string FormatDiarizedTranscript(const std::vector<SpeakerSegment>& segments);

}  // namespace EDGESCRIBE
