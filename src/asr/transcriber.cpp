// EDGESCRIBE — Transcriber Engine Implementation

#include "asr/transcriber.h"
#include "ort_genai.h"
#include <iostream>
#include <iomanip>
#include <sstream>
#include <cstring>

namespace EDGESCRIBE {

// Create model with optional device/EP selection
static std::unique_ptr<OgaModel> CreateModelWithDevice(
    const std::string& model_path, const std::string& device) {
  if (device.empty() || device == "cpu") {
    return OgaModel::Create(model_path.c_str());
  }
  auto config = OgaConfig::Create(model_path.c_str());
  config->ClearProviders();
  config->AppendProvider(device.c_str());
  config->AppendProvider("cpu");  // CPU fallback
  return OgaModel::Create(*config);
}

Transcriber::Transcriber(const std::string& model_path,
                         const std::string& device) {
  model_ = CreateModelWithDevice(model_path, device);
  processor_ = OgaStreamingProcessor::Create(*model_);
  tokenizer_ = OgaTokenizer::Create(*model_);
  token_stream_ = OgaTokenizerStream::Create(*tokenizer_);
  params_ = OgaGeneratorParams::Create(*model_);
  generator_ = OgaGenerator::Create(*model_, *params_);
}

Transcriber::~Transcriber() = default;

std::string Transcriber::ProcessChunk(const float* samples, size_t count) {
  std::lock_guard<std::mutex> lock(mutex_);

  total_samples_fed_ += count;

  auto inputs = processor_->Process(samples, count);
  if (inputs) {
    generator_->SetInputs(*inputs);
    return DecodeTokens();
  }
  return "";
}

std::string Transcriber::Flush() {
  std::lock_guard<std::mutex> lock(mutex_);
  std::string result;

  // Flush remaining buffered audio
  auto flush_inputs = processor_->Flush();
  if (flush_inputs) {
    generator_->SetInputs(*flush_inputs);
    result += DecodeTokens();
  }

  // Feed silence chunks for right context
  std::vector<float> silence(kChunkSize, 0.0f);
  for (int i = 0; i < kSilenceChunksForRightContext; i++) {
    auto silence_inputs = processor_->Process(silence.data(), silence.size());
    if (silence_inputs) {
      generator_->SetInputs(*silence_inputs);
      result += DecodeTokens();
    }
  }

  // Flush any remaining word in the buffer
  if (!current_word_buffer_.empty()) {
    double current_time = static_cast<double>(total_samples_fed_) / kSampleRate;
    TimestampedWord word;
    word.text = current_word_buffer_;
    word.start_time = last_token_time_;
    word.end_time = current_time;
    timestamped_words_.push_back(word);
    current_word_buffer_.clear();
  }

  return result;
}

void Transcriber::Reset() {
  std::lock_guard<std::mutex> lock(mutex_);
  transcript_.clear();
  timestamped_words_.clear();
  total_samples_fed_ = 0;
  last_token_time_ = 0.0;
  current_word_buffer_.clear();

  // Re-create the pipeline for a fresh session
  processor_ = OgaStreamingProcessor::Create(*model_);
  token_stream_ = OgaTokenizerStream::Create(*tokenizer_);
  params_ = OgaGeneratorParams::Create(*model_);
  generator_ = OgaGenerator::Create(*model_, *params_);
}

std::string Transcriber::DecodeTokens() {
  std::string new_text;
  double current_time = static_cast<double>(total_samples_fed_) / kSampleRate;

  while (!generator_->IsDone()) {
    generator_->GenerateNextToken();
    auto tokens = generator_->GetNextTokens();

    if (!tokens.empty()) {
      const char* text = token_stream_->Decode(tokens[0]);
      if (text && text[0] != '\0') {
        std::string token_text(text);
        new_text += token_text;
        transcript_ += token_text;

        // Build words from tokens — split on spaces
        for (char c : token_text) {
          if (c == ' ' || c == '\n') {
            // End of word — save with timestamp
            if (!current_word_buffer_.empty()) {
              TimestampedWord word;
              word.text = current_word_buffer_;
              word.start_time = last_token_time_;
              word.end_time = current_time;
              timestamped_words_.push_back(word);
              last_token_time_ = current_time;
              current_word_buffer_.clear();
            }
          } else {
            if (current_word_buffer_.empty()) {
              last_token_time_ = current_time;
            }
            current_word_buffer_ += c;
          }
        }

        if (callback_) {
          callback_(token_text);
        }
      }
    }
  }

  return new_text;
}

// ── SRT Export ──────────────────────────────────────────────────────────────
static std::string FormatSrtTime(double seconds) {
  int h = static_cast<int>(seconds / 3600);
  int m = static_cast<int>(std::fmod(seconds, 3600) / 60);
  int s = static_cast<int>(std::fmod(seconds, 60));
  int ms = static_cast<int>(std::fmod(seconds, 1.0) * 1000);
  std::ostringstream ss;
  ss << std::setfill('0')
     << std::setw(2) << h << ":"
     << std::setw(2) << m << ":"
     << std::setw(2) << s << ","
     << std::setw(3) << ms;
  return ss.str();
}

static std::string FormatVttTime(double seconds) {
  int h = static_cast<int>(seconds / 3600);
  int m = static_cast<int>(std::fmod(seconds, 3600) / 60);
  int s = static_cast<int>(std::fmod(seconds, 60));
  int ms = static_cast<int>(std::fmod(seconds, 1.0) * 1000);
  std::ostringstream ss;
  ss << std::setfill('0')
     << std::setw(2) << h << ":"
     << std::setw(2) << m << ":"
     << std::setw(2) << s << "."
     << std::setw(3) << ms;
  return ss.str();
}

std::string Transcriber::ExportSRT(int max_chars_per_line) const {
  if (timestamped_words_.empty()) return "";

  std::ostringstream ss;
  int subtitle_index = 1;

  // Group words into subtitle lines
  std::string current_line;
  double line_start = timestamped_words_[0].start_time;
  double line_end = line_start;

  for (const auto& word : timestamped_words_) {
    std::string candidate = current_line.empty()
        ? word.text
        : current_line + " " + word.text;

    if (static_cast<int>(candidate.size()) > max_chars_per_line &&
        !current_line.empty()) {
      // Emit current line as subtitle
      ss << subtitle_index++ << "\n"
         << FormatSrtTime(line_start) << " --> "
         << FormatSrtTime(line_end) << "\n"
         << current_line << "\n\n";

      current_line = word.text;
      line_start = word.start_time;
    } else {
      current_line = candidate;
    }
    line_end = word.end_time;
  }

  // Emit remaining
  if (!current_line.empty()) {
    ss << subtitle_index << "\n"
       << FormatSrtTime(line_start) << " --> "
       << FormatSrtTime(line_end) << "\n"
       << current_line << "\n\n";
  }

  return ss.str();
}

std::string Transcriber::ExportVTT(int max_chars_per_line) const {
  if (timestamped_words_.empty()) return "";

  std::ostringstream ss;
  ss << "WEBVTT\n\n";

  std::string current_line;
  double line_start = timestamped_words_[0].start_time;
  double line_end = line_start;

  for (const auto& word : timestamped_words_) {
    std::string candidate = current_line.empty()
        ? word.text
        : current_line + " " + word.text;

    if (static_cast<int>(candidate.size()) > max_chars_per_line &&
        !current_line.empty()) {
      ss << FormatVttTime(line_start) << " --> "
         << FormatVttTime(line_end) << "\n"
         << current_line << "\n\n";

      current_line = word.text;
      line_start = word.start_time;
    } else {
      current_line = candidate;
    }
    line_end = word.end_time;
  }

  if (!current_line.empty()) {
    ss << FormatVttTime(line_start) << " --> "
       << FormatVttTime(line_end) << "\n"
       << current_line << "\n\n";
  }

  return ss.str();
}

}  // namespace EDGESCRIBE
