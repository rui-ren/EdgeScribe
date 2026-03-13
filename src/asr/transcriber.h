// EDGESCRIBE — Transcriber Engine
// Wraps onnxruntime-genai StreamingProcessor + Generator pipeline

#pragma once

#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

// Forward declarations — avoid including ort_genai.h in header
struct OgaModel;
struct OgaStreamingProcessor;
struct OgaGenerator;
struct OgaGeneratorParams;
struct OgaTokenizer;
struct OgaTokenizerStream;

namespace EDGESCRIBE {

// A timestamped word/segment in the transcript
struct TimestampedWord {
  std::string text;
  double start_time;  // seconds from beginning of audio
  double end_time;    // seconds
};

// Callback for receiving partial transcript text as it's decoded
using TranscriptCallback = std::function<void(const std::string& text)>;

class Transcriber {
 public:
  // device: "cpu", "cuda", "dml", "vulkan", "rocm", "qnn", "coreml"
  explicit Transcriber(const std::string& model_path,
                       const std::string& device = "cpu");
  ~Transcriber();

  // Feed a chunk of float32 PCM audio (16kHz, mono)
  // Returns any new text decoded from this chunk
  std::string ProcessChunk(const float* samples, size_t count);

  // Flush remaining buffered audio and decode final tokens
  std::string Flush();

  // Get the full accumulated transcript
  const std::string& GetTranscript() const { return transcript_; }

  // Get timestamped words
  const std::vector<TimestampedWord>& GetTimestampedWords() const {
    return timestamped_words_;
  }

  // Export transcript as SRT subtitle format
  std::string ExportSRT(int max_chars_per_line = 42) const;

  // Export transcript as VTT (WebVTT) subtitle format
  std::string ExportVTT(int max_chars_per_line = 42) const;

  // Reset for a new transcription session
  void Reset();

  // Set callback for real-time text output
  void SetCallback(TranscriptCallback callback) { callback_ = std::move(callback); }

  // Get current audio position in seconds
  double GetCurrentTime() const { return total_samples_fed_ / 16000.0; }

 private:
  std::string DecodeTokens();

  std::unique_ptr<OgaModel> model_;
  std::unique_ptr<OgaStreamingProcessor> processor_;
  std::unique_ptr<OgaGenerator> generator_;
  std::unique_ptr<OgaGeneratorParams> params_;
  std::unique_ptr<OgaTokenizer> tokenizer_;
  std::unique_ptr<OgaTokenizerStream> token_stream_;

  std::string transcript_;
  std::vector<TimestampedWord> timestamped_words_;
  size_t total_samples_fed_ = 0;     // Total audio samples processed
  double last_token_time_ = 0.0;     // Time of the last decoded token
  std::string current_word_buffer_;   // Accumulates tokens into words

  std::mutex mutex_;
  TranscriptCallback callback_;

  static constexpr int kSilenceChunksForRightContext = 4;
  static constexpr int kChunkSize = 8960;  // 560ms at 16kHz
  static constexpr int kSampleRate = 16000;
};

}  // namespace EDGESCRIBE
