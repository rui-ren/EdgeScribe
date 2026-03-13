// EDGESCRIBE — On-device AI assistant. Speech, vision, language.
//
// Usage:
//   EDGESCRIBE pull nemotron              Download a model
//   EDGESCRIBE run --live                 Live microphone transcription
//   EDGESCRIBE run meeting.wav            Transcribe a WAV file
//   EDGESCRIBE chat "prompt"              Chat with local LLM
//   EDGESCRIBE vision image.jpg           Analyze image / OCR
//   EDGESCRIBE process --soap file.txt    Generate SOAP notes
//   EDGESCRIBE list                       List available/downloaded models

#include "asr/audio_capture.h"
#include "asr/audio_file.h"
#include "core/model_manager.h"
#include "asr/transcriber.h"
#include "asr/diarizer.h"
#include "llm/llm_engine.h"
#include "vision/vision_engine.h"
#include "tts/tts_engine.h"
#include "server/api_server.h"

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

using namespace EDGESCRIBE;

static std::atomic<bool> g_running{true};

static void SignalHandler(int) {
  g_running.store(false);
}

static void PrintVersion() {
#ifdef EDGESCRIBE_VERSION
  std::cout << "edgescribe v" << EDGESCRIBE_VERSION << std::endl;
#else
  std::cout << "edgescribe v0.1.0" << std::endl;
#endif
}

static void PrintUsage() {
  PrintVersion();
  std::cout << "On-device AI assistant. Speech, vision, language. Private. Open source." << std::endl;
  std::cout << std::endl;
  std::cout << "Commands:" << std::endl;
  std::cout << "  EDGESCRIBE pull <model>              Download a model from HuggingFace" << std::endl;
  std::cout << "  EDGESCRIBE list                       List available and downloaded models" << std::endl;
  std::cout << "  EDGESCRIBE remove <model>             Delete a downloaded model" << std::endl;
  std::cout << std::endl;
  std::cout << "Speech-to-Text (ASR):" << std::endl;
  std::cout << "  EDGESCRIBE run --live [options]       Live microphone transcription" << std::endl;
  std::cout << "  EDGESCRIBE run <file.wav> [options]   Transcribe a WAV file" << std::endl;
  std::cout << "  EDGESCRIBE devices                    List audio input devices" << std::endl;
  std::cout << std::endl;
  std::cout << "Vision & Language:" << std::endl;
  std::cout << "  EDGESCRIBE vision <image> [--prompt]  Analyze image (OCR, understanding)" << std::endl;
  std::cout << "  EDGESCRIBE chat <prompt>              Chat with local LLM" << std::endl;
  std::cout << "  EDGESCRIBE process --soap <file>      Generate SOAP notes from transcript" << std::endl;
  std::cout << std::endl;
  std::cout << "Text-to-Speech:" << std::endl;
  std::cout << "  EDGESCRIBE speak <text|file>          Read text aloud" << std::endl;
  std::cout << std::endl;
  std::cout << "Server:" << std::endl;
  std::cout << "  EDGESCRIBE serve [--port 8080]        Start REST API server" << std::endl;
  std::cout << std::endl;
  std::cout << "Options:" << std::endl;
  std::cout << "  --model <name|path>    Model to use (default: nemotron for ASR)" << std::endl;
  std::cout << "  --device <ep>          Device: cpu, cuda, dml, vulkan, rocm, qnn, coreml" << std::endl;
  std::cout << "  -o <file>              Write output to file" << std::endl;
  std::cout << "  --version              Show version" << std::endl;
  std::cout << "  --help                 Show this help" << std::endl;
  std::cout << std::endl;
  std::cout << "Examples:" << std::endl;
  std::cout << "  EDGESCRIBE pull nemotron                 # Download ASR model (~670 MB)" << std::endl;
  std::cout << "  EDGESCRIBE pull qwen3-vl                 # Download vision+LLM (~1.5 GB)" << std::endl;
  std::cout << "  EDGESCRIBE pull kokoro                   # Download TTS (~300 MB)" << std::endl;
  std::cout << "  EDGESCRIBE run --live                    # Live transcription" << std::endl;
  std::cout << "  EDGESCRIBE run meeting.wav -o notes.txt  # File transcription" << std::endl;
}

// ── Command: pull ──────────────────────────────────────────────────────────
static int CmdPull(const std::string& model_name) {
  try {
    ModelManager manager;
    manager.Pull(model_name);
    return 0;
  } catch (const std::exception& e) {
    std::cerr << "Error: " << e.what() << std::endl;
    return 1;
  }
}

// ── Command: list ──────────────────────────────────────────────────────────
static int CmdList() {
  ModelManager manager;
  auto models = manager.ListAvailable();

  std::cout << "Available models:" << std::endl;
  std::cout << std::endl;

  // Group by type
  const char* types[] = {"asr", "vlm", "tts", "diarizer"};
  const char* labels[] = {
      "Speech-to-Text (ASR)",
      "Vision + Language (VLM)",
      "Text-to-Speech (TTS)",
      "Speaker Diarization"
  };

  for (int t = 0; t < 4; t++) {
    std::cout << "  " << labels[t] << ":" << std::endl;
    for (const auto& m : models) {
      if (m.type == types[t]) {
        std::string status = m.cached ? " [downloaded]" : "";
        std::cout << "    " << std::left << std::setw(16) << m.name
                  << std::setw(22) << m.display_name
                  << "~" << std::setw(6) << std::to_string(m.size_mb) + " MB"
                  << status << std::endl;
        std::cout << "    " << std::string(16, ' ') << m.description << std::endl;
      }
    }
    std::cout << std::endl;
  }

  auto cached = manager.ListCached();
  if (!cached.empty()) {
    std::cout << "Cache directory: " << manager.GetCacheDir() << std::endl;
  }

  return 0;
}

// ── Command: remove ────────────────────────────────────────────────────────
static int CmdRemove(const std::string& model_name) {
  try {
    ModelManager manager;
    manager.Remove(model_name);
    return 0;
  } catch (const std::exception& e) {
    std::cerr << "Error: " << e.what() << std::endl;
    return 1;
  }
}

// ── Command: devices ───────────────────────────────────────────────────────
static int CmdDevices() {
  auto devices = AudioCapture::ListDevices();

  if (devices.empty()) {
    std::cout << "No audio input devices found." << std::endl;
    return 1;
  }

  std::cout << "Audio input devices:" << std::endl;
  for (size_t i = 0; i < devices.size(); i++) {
    std::cout << "  [" << i << "] " << devices[i] << std::endl;
  }

  return 0;
}

// ── Command: run --live ────────────────────────────────────────────────────
static int CmdRunLive(const std::string& model_path,
                      const std::string& output_file,
                      const std::string& device = "cpu") {
  try {
    std::cout << "Loading model" << (device != "cpu" ? " (" + device + ")" : "") << "..." << std::endl;
    Transcriber transcriber(model_path, device);

    // Set up real-time output
    transcriber.SetCallback([](const std::string& text) {
      std::cout << text << std::flush;
    });

    AudioCapture capture;

    std::cout << std::endl;
    std::cout << "════════════════════════════════════════════════════════════" << std::endl;
    std::cout << "  EDGESCRIBE — Live Transcription" << std::endl;
    std::cout << "  Speak into your microphone. Text appears in real-time." << std::endl;
    std::cout << "  Press Ctrl+C to stop." << std::endl;
    std::cout << "════════════════════════════════════════════════════════════" << std::endl;
    std::cout << std::endl;

    auto start_time = std::chrono::steady_clock::now();
    size_t total_samples = 0;

    // Start mic capture → feed to transcriber
    capture.Start([&](const float* samples, size_t count) {
      transcriber.ProcessChunk(samples, count);
      total_samples += count;
    });

    // Wait for Ctrl+C
    std::signal(SIGINT, SignalHandler);
#ifdef _WIN32
    std::signal(SIGBREAK, SignalHandler);
#endif

    while (g_running.load()) {
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    // Stop and flush
    capture.Stop();
    std::string final_text = transcriber.Flush();

    auto end_time = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::seconds>(end_time - start_time);

    std::cout << std::endl;
    std::cout << std::endl;
    std::cout << "════════════════════════════════════════════════════════════" << std::endl;
    std::cout << "  Duration: " << duration.count() << "s" << std::endl;
    std::cout << "  Transcript:" << std::endl;
    std::cout << "  " << transcriber.GetTranscript() << std::endl;
    std::cout << "════════════════════════════════════════════════════════════" << std::endl;

    // Save to file if requested
    if (!output_file.empty()) {
      std::ofstream ofs(output_file);
      if (ofs.is_open()) {
        ofs << transcriber.GetTranscript() << std::endl;
        std::cout << "Saved to: " << output_file << std::endl;
      } else {
        std::cerr << "Error: Cannot write to " << output_file << std::endl;
        return 1;
      }
    }

    return 0;
  } catch (const std::exception& e) {
    std::cerr << "Error: " << e.what() << std::endl;
    return 1;
  }
}

// ── Command: run <file> ────────────────────────────────────────────────────
static int CmdRunFile(const std::string& audio_path,
                      const std::string& model_path,
                      const std::string& output_file,
                      const std::string& device = "cpu",
                      const std::string& srt_file = "",
                      const std::string& vtt_file = "") {
  try {
    std::cout << "Loading audio: " << audio_path << std::endl;
    auto audio = LoadWavFile(audio_path);
    std::cout << "Audio: " << std::fixed << std::setprecision(1)
              << audio.duration_seconds << "s ("
              << audio.samples.size() << " samples)" << std::endl;

    std::cout << "Loading model" << (device != "cpu" ? " (" + device + ")" : "") << "..." << std::endl;
    Transcriber transcriber(model_path, device);

    // Set up real-time output
    transcriber.SetCallback([](const std::string& text) {
      std::cout << text << std::flush;
    });

    std::cout << std::string(60, '-') << std::endl;

    auto start_time = std::chrono::steady_clock::now();

    // Feed audio in chunks
    constexpr size_t kChunkSize = 8960;  // 560ms at 16kHz
    for (size_t i = 0; i < audio.samples.size(); i += kChunkSize) {
      size_t remaining = std::min(kChunkSize, audio.samples.size() - i);
      transcriber.ProcessChunk(audio.samples.data() + i, remaining);
    }

    // Flush
    transcriber.Flush();

    auto end_time = std::chrono::steady_clock::now();
    auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);

    std::cout << std::endl;
    std::cout << std::string(60, '=') << std::endl;
    std::cout << "  " << transcriber.GetTranscript() << std::endl;
    std::cout << std::string(60, '=') << std::endl;

    double rtf = (elapsed_ms.count() / 1000.0) / audio.duration_seconds;
    std::cout << "  Time: " << elapsed_ms.count() << "ms | RTF: "
              << std::fixed << std::setprecision(2) << rtf << "x" << std::endl;

    // Save to file if requested
    if (!output_file.empty()) {
      std::ofstream ofs(output_file);
      if (ofs.is_open()) {
        ofs << transcriber.GetTranscript() << std::endl;
        std::cout << "  Saved to: " << output_file << std::endl;
      } else {
        std::cerr << "Error: Cannot write to " << output_file << std::endl;
        return 1;
      }
    }

    // Export subtitles
    if (!srt_file.empty()) {
      std::ofstream ofs(srt_file);
      if (ofs.is_open()) {
        ofs << transcriber.ExportSRT();
        std::cout << "  SRT saved to: " << srt_file << std::endl;
      }
    }
    if (!vtt_file.empty()) {
      std::ofstream ofs(vtt_file);
      if (ofs.is_open()) {
        ofs << transcriber.ExportVTT();
        std::cout << "  VTT saved to: " << vtt_file << std::endl;
      }
    }

    // Show word count with timestamps
    auto words = transcriber.GetTimestampedWords();
    if (!words.empty()) {
      std::cout << "  Words: " << words.size()
                << " | Duration: " << std::fixed << std::setprecision(1)
                << words.back().end_time << "s" << std::endl;
    }

    return 0;
  } catch (const std::exception& e) {
    std::cerr << "Error: " << e.what() << std::endl;
    return 1;
  }
}

// ── Command: run <file> --diarize ──────────────────────────────────────────
static int CmdRunFileDiarized(const std::string& audio_path,
                              const std::string& model_path,
                              const std::string& diarizer_path,
                              const std::string& output_file) {
  try {
    std::cout << "Loading audio: " << audio_path << std::endl;
    auto audio = LoadWavFile(audio_path);
    std::cout << "Audio: " << std::fixed << std::setprecision(1)
              << audio.duration_seconds << "s ("
              << audio.samples.size() << " samples)" << std::endl;

    std::cout << "Loading ASR model..." << std::endl;
    Transcriber transcriber(model_path);

    std::cout << "Loading speaker model..." << std::endl;
    EDGESCRIBE::asr::Diarizer diarizer(diarizer_path);

    std::cout << std::string(60, '-') << std::endl;

    auto start_time = std::chrono::steady_clock::now();

    // Process audio in chunks — transcribe + identify speaker per chunk
    constexpr size_t kChunkSize = 8960;
    // Use larger segments for speaker embedding (better accuracy with more audio)
    constexpr size_t kSpeakerSegmentSize = 16000 * 2;  // 2 seconds

    std::string full_output;
    std::vector<float> speaker_buffer;
    int last_speaker = -1;

    for (size_t i = 0; i < audio.samples.size(); i += kChunkSize) {
      size_t remaining = std::min(kChunkSize, audio.samples.size() - i);
      const float* chunk = audio.samples.data() + i;

      // Accumulate for speaker embedding
      speaker_buffer.insert(speaker_buffer.end(), chunk, chunk + remaining);

      // Transcribe
      std::string text = transcriber.ProcessChunk(chunk, remaining);

      // When we have enough audio for speaker identification
      if (!text.empty() && speaker_buffer.size() >= kSpeakerSegmentSize) {
        auto segment = diarizer.IdentifySpeaker(
            speaker_buffer.data(), speaker_buffer.size());
        speaker_buffer.clear();

        if (segment.speaker_id != last_speaker) {
          if (!full_output.empty()) std::cout << std::endl;
          std::cout << "[" << segment.speaker_label << "] ";
          full_output += "\n[" + segment.speaker_label + "] ";
          last_speaker = segment.speaker_id;
        }
        std::cout << text << std::flush;
        full_output += text;
      } else if (!text.empty()) {
        std::cout << text << std::flush;
        full_output += text;
      }
    }

    // Flush remaining ASR
    std::string flush_text = transcriber.Flush();
    if (!flush_text.empty()) {
      // Final speaker check on remaining buffer
      if (!speaker_buffer.empty()) {
        auto segment = diarizer.IdentifySpeaker(
            speaker_buffer.data(), speaker_buffer.size());
        if (segment.speaker_id != last_speaker) {
          std::cout << std::endl << "[" << segment.speaker_label << "] ";
          full_output += "\n[" + segment.speaker_label + "] ";
        }
      }
      std::cout << flush_text;
      full_output += flush_text;
    }

    auto end_time = std::chrono::steady_clock::now();
    auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        end_time - start_time);

    std::cout << std::endl;
    std::cout << std::string(60, '=') << std::endl;
    std::cout << "  Speakers detected: " << diarizer.GetSpeakerCount() << std::endl;
    double rtf = (elapsed_ms.count() / 1000.0) / audio.duration_seconds;
    std::cout << "  Time: " << elapsed_ms.count() << "ms | RTF: "
              << std::fixed << std::setprecision(2) << rtf << "x" << std::endl;

    if (!output_file.empty()) {
      std::ofstream ofs(output_file);
      if (ofs.is_open()) {
        ofs << full_output << std::endl;
        std::cout << "  Saved to: " << output_file << std::endl;
      }
    }

    return 0;
  } catch (const std::exception& e) {
    std::cerr << "Error: " << e.what() << std::endl;
    return 1;
  }
}

// ── Argument parsing ───────────────────────────────────────────────────────
int main(int argc, char* argv[]) {
  if (argc < 2) {
    PrintUsage();
    return 0;
  }

  std::string command = argv[1];

  // Handle flags
  if (command == "--help" || command == "-h") {
    PrintUsage();
    return 0;
  }
  if (command == "--version" || command == "-v") {
    PrintVersion();
    return 0;
  }

  // ── pull ──
  if (command == "pull") {
    if (argc < 3) {
      std::cerr << "Usage: EDGESCRIBE pull <model>" << std::endl;
      return 1;
    }
    return CmdPull(argv[2]);
  }

  // ── list ──
  if (command == "list") {
    return CmdList();
  }

  // ── remove ──
  if (command == "remove" || command == "rm") {
    if (argc < 3) {
      std::cerr << "Usage: EDGESCRIBE remove <model>" << std::endl;
      return 1;
    }
    return CmdRemove(argv[2]);
  }

  // ── devices ──
  if (command == "devices") {
    return CmdDevices();
  }

  // ── chat ──
  if (command == "chat") {
    std::string model_name = "qwen3-vl";
    std::string output_file;
    std::string device = "cpu";
    std::string prompt;

    for (int i = 2; i < argc; i++) {
      std::string arg = argv[i];
      if (arg == "--model" || arg == "-m") {
        if (i + 1 < argc) model_name = argv[++i];
      } else if (arg == "-o" || arg == "--output") {
        if (i + 1 < argc) output_file = argv[++i];
      } else if (arg == "--device" || arg == "-d") {
        if (i + 1 < argc) device = argv[++i];
      } else if (arg[0] != '-') {
        if (!prompt.empty()) prompt += " ";
        prompt += arg;
      }
    }

    if (prompt.empty()) {
      std::cerr << "Usage: EDGESCRIBE chat \"your prompt here\"" << std::endl;
      return 1;
    }

    try {
      ModelManager manager;
      std::string model_path = manager.GetModelPath(model_name);

      std::cout << "Loading model" << (device != "cpu" ? " (" + device + ")" : "") << "..." << std::endl;
      EDGESCRIBE::llm::LlmEngine engine(model_path, device);

      std::cout << std::string(60, '-') << std::endl;
      std::string result = engine.Chat(
          "You are a helpful assistant.",
          prompt,
          2048,
          [](const std::string& token) { std::cout << token << std::flush; });

      std::cout << std::endl;

      if (!output_file.empty()) {
        std::ofstream ofs(output_file);
        if (ofs.is_open()) {
          ofs << result << std::endl;
          std::cout << "Saved to: " << output_file << std::endl;
        }
      }
      return 0;
    } catch (const std::exception& e) {
      std::cerr << "Error: " << e.what() << std::endl;
      return 1;
    }
  }

  // ── vision ──
  if (command == "vision") {
    std::string model_name = "qwen3-vl";
    std::string output_file;
    std::string prompt = "Describe this image in detail.";
    std::string image_path;
    bool ocr_mode = false;

    for (int i = 2; i < argc; i++) {
      std::string arg = argv[i];
      if (arg == "--model" || arg == "-m") {
        if (i + 1 < argc) model_name = argv[++i];
      } else if (arg == "--prompt" || arg == "-p") {
        if (i + 1 < argc) prompt = argv[++i];
      } else if (arg == "--ocr") {
        ocr_mode = true;
      } else if (arg == "-o" || arg == "--output") {
        if (i + 1 < argc) output_file = argv[++i];
      } else if (arg[0] != '-') {
        image_path = arg;
      }
    }

    if (image_path.empty()) {
      std::cerr << "Usage: EDGESCRIBE vision <image.jpg> [--prompt \"...\"]" << std::endl;
      std::cerr << "       EDGESCRIBE vision <image.jpg> --ocr" << std::endl;
      return 1;
    }

    try {
      ModelManager manager;
      std::string model_path = manager.GetModelPath(model_name);

      std::cout << "Loading model..." << std::endl;
      EDGESCRIBE::vision::VisionEngine engine(model_path);

      std::cout << "Analyzing: " << image_path << std::endl;
      std::cout << std::string(60, '-') << std::endl;

      std::string result;
      auto callback = [](const std::string& token) {
        std::cout << token << std::flush;
      };

      if (ocr_mode) {
        result = engine.OCR(image_path, callback);
      } else {
        result = engine.Analyze(image_path, prompt, 2048, callback);
      }

      std::cout << std::endl;

      if (!output_file.empty()) {
        std::ofstream ofs(output_file);
        if (ofs.is_open()) {
          ofs << result << std::endl;
          std::cout << "Saved to: " << output_file << std::endl;
        }
      }
      return 0;
    } catch (const std::exception& e) {
      std::cerr << "Error: " << e.what() << std::endl;
      return 1;
    }
  }

  // ── process ──
  if (command == "process") {
    std::string model_name = "qwen3-vl";
    std::string output_file;
    std::string input_file;
    std::string image_path;
    bool soap_mode = false;
    bool summarize_mode = false;
    bool fix_terms = false;

    for (int i = 2; i < argc; i++) {
      std::string arg = argv[i];
      if (arg == "--soap") {
        soap_mode = true;
      } else if (arg == "--summarize") {
        summarize_mode = true;
      } else if (arg == "--fix-terms") {
        fix_terms = true;
      } else if (arg == "--model" || arg == "-m") {
        if (i + 1 < argc) model_name = argv[++i];
      } else if (arg == "--image") {
        if (i + 1 < argc) image_path = argv[++i];
      } else if (arg == "-o" || arg == "--output") {
        if (i + 1 < argc) output_file = argv[++i];
      } else if (arg[0] != '-') {
        input_file = arg;
      }
    }

    if (input_file.empty() || (!soap_mode && !summarize_mode && !fix_terms)) {
      std::cerr << "Usage: EDGESCRIBE process --soap <transcript.txt>" << std::endl;
      std::cerr << "       EDGESCRIBE process --summarize <file.txt>" << std::endl;
      std::cerr << "       EDGESCRIBE process --fix-terms <transcript.txt>" << std::endl;
      return 1;
    }

    // Read input file
    std::ifstream ifs(input_file);
    if (!ifs.is_open()) {
      std::cerr << "Error: Cannot read " << input_file << std::endl;
      return 1;
    }
    std::string content((std::istreambuf_iterator<char>(ifs)),
                         std::istreambuf_iterator<char>());

    try {
      ModelManager manager;
      std::string model_path = manager.GetModelPath(model_name);

      std::cout << "Loading model..." << std::endl;

      auto callback = [](const std::string& token) {
        std::cout << token << std::flush;
      };

      std::string result;
      std::cout << std::string(60, '-') << std::endl;

      if (soap_mode && !image_path.empty()) {
        // Use vision engine for SOAP with image
        EDGESCRIBE::vision::VisionEngine engine(model_path);
        result = engine.GenerateSOAPNotes(content, image_path, callback);
      } else {
        // Use LLM engine for text-only processing
        EDGESCRIBE::llm::LlmEngine engine(model_path);
        if (soap_mode) {
          result = engine.GenerateSOAPNotes(content, callback);
        } else if (summarize_mode) {
          result = engine.Summarize(content, callback);
        } else if (fix_terms) {
          result = engine.FixMedicalTerms(content, callback);
        }
      }

      std::cout << std::endl;

      if (!output_file.empty()) {
        std::ofstream ofs(output_file);
        if (ofs.is_open()) {
          ofs << result << std::endl;
          std::cout << "Saved to: " << output_file << std::endl;
        }
      }
      return 0;
    } catch (const std::exception& e) {
      std::cerr << "Error: " << e.what() << std::endl;
      return 1;
    }
  }

  // ── run ──
  if (command == "run") {
    // Parse run options
    std::string model_name = "nemotron";
    std::string output_file;
    std::string audio_file;
    std::string device = "cpu";
    std::string srt_file;
    std::string vtt_file;
    bool live_mode = false;
    bool diarize = false;

    for (int i = 2; i < argc; i++) {
      std::string arg = argv[i];

      if (arg == "--live") {
        live_mode = true;
      } else if (arg == "--diarize") {
        diarize = true;
      } else if (arg == "--device" || arg == "-d") {
        if (i + 1 < argc) {
          device = argv[++i];
        } else {
          std::cerr << "Error: --device requires a value (cpu, cuda, dml, vulkan, rocm, qnn, coreml)" << std::endl;
          return 1;
        }
      } else if (arg == "--srt") {
        if (i + 1 < argc) {
          srt_file = argv[++i];
        } else {
          std::cerr << "Error: --srt requires a filename" << std::endl;
          return 1;
        }
      } else if (arg == "--vtt") {
        if (i + 1 < argc) {
          vtt_file = argv[++i];
        } else {
          std::cerr << "Error: --vtt requires a filename" << std::endl;
          return 1;
        }
      } else if (arg == "--model" || arg == "-m") {
        if (i + 1 < argc) {
          model_name = argv[++i];
        } else {
          std::cerr << "Error: --model requires a value" << std::endl;
          return 1;
        }
      } else if (arg == "-o" || arg == "--output") {
        if (i + 1 < argc) {
          output_file = argv[++i];
        } else {
          std::cerr << "Error: -o requires a filename" << std::endl;
          return 1;
        }
      } else if (arg[0] != '-') {
        audio_file = arg;
      } else {
        std::cerr << "Unknown option: " << arg << std::endl;
        return 1;
      }
    }

    // Resolve model path
    ModelManager manager;
    std::string model_path;
    try {
      model_path = manager.GetModelPath(model_name);
    } catch (const std::exception& e) {
      std::cerr << "Error: " << e.what() << std::endl;
      return 1;
    }

    // Resolve diarizer model if requested
    std::string diarizer_path;
    if (diarize) {
      try {
        diarizer_path = manager.GetModelPath("speakers");
      } catch (const std::exception& e) {
        std::cerr << "Error: Speaker model not found. Run: EDGESCRIBE pull speakers" << std::endl;
        return 1;
      }
    }

    if (live_mode) {
      return CmdRunLive(model_path, output_file, device);
    } else if (!audio_file.empty()) {
      if (diarize && !diarizer_path.empty()) {
        return CmdRunFileDiarized(audio_file, model_path, diarizer_path, output_file);
      }
      return CmdRunFile(audio_file, model_path, output_file, device, srt_file, vtt_file);
    } else {
      std::cerr << "Usage: EDGESCRIBE run --live" << std::endl;
      std::cerr << "       EDGESCRIBE run <file.wav> [--diarize]" << std::endl;
      return 1;
    }
  }

  // ── speak ──
  if (command == "speak") {
    std::string model_name = "kokoro";
    std::string output_file;
    std::string voice = "af_heart";
    float speed = 1.0f;
    std::string text;

    for (int i = 2; i < argc; i++) {
      std::string arg = argv[i];
      if (arg == "--model" || arg == "-m") {
        if (i + 1 < argc) model_name = argv[++i];
      } else if (arg == "--voice" || arg == "-v") {
        if (i + 1 < argc) voice = argv[++i];
      } else if (arg == "--speed") {
        if (i + 1 < argc) speed = std::stof(argv[++i]);
      } else if (arg == "-o" || arg == "--output") {
        if (i + 1 < argc) output_file = argv[++i];
      } else if (arg == "--voices") {
        // List voices mode
        try {
          ModelManager manager;
          std::string model_path = manager.GetModelPath(model_name);
          EDGESCRIBE::tts::TtsEngine engine(model_path);
          auto voices_list = engine.ListVoices();
          std::cout << "Available voices:" << std::endl;
          for (const auto& v : voices_list) {
            std::cout << "  " << v << std::endl;
          }
          return 0;
        } catch (const std::exception& e) {
          std::cerr << "Error: " << e.what() << std::endl;
          return 1;
        }
      } else if (arg[0] != '-') {
        // Check if it's a file path
        if (std::filesystem::exists(arg)) {
          std::ifstream ifs(arg);
          text = std::string((std::istreambuf_iterator<char>(ifs)),
                              std::istreambuf_iterator<char>());
        } else {
          if (!text.empty()) text += " ";
          text += arg;
        }
      }
    }

    if (text.empty()) {
      std::cerr << "Usage: EDGESCRIBE speak \"Hello world\"" << std::endl;
      std::cerr << "       EDGESCRIBE speak notes.txt" << std::endl;
      std::cerr << "       EDGESCRIBE speak \"Hello\" -o output.wav" << std::endl;
      std::cerr << "       EDGESCRIBE speak --voices" << std::endl;
      return 1;
    }

    try {
      ModelManager manager;
      std::string model_path = manager.GetModelPath(model_name);

      std::cout << "Loading TTS model..." << std::endl;
      EDGESCRIBE::tts::TtsEngine engine(model_path);

      if (!output_file.empty()) {
        std::cout << "Synthesizing to: " << output_file << std::endl;
        engine.SynthesizeToFile(text, output_file, voice, speed);
        std::cout << "Done." << std::endl;
      } else {
        std::cout << "Speaking..." << std::endl;
        engine.Speak(text, voice, speed);
      }
      return 0;
    } catch (const std::exception& e) {
      std::cerr << "Error: " << e.what() << std::endl;
      return 1;
    }
  }

  // ── serve ──
  if (command == "serve") {
    int port = 8080;
    std::string host = "127.0.0.1";
    std::string device = "cpu";
    std::string asr_model;
    std::string vlm_model;
    std::string tts_model;

    for (int i = 2; i < argc; i++) {
      std::string arg = argv[i];
      if (arg == "--port" || arg == "-p") {
        if (i + 1 < argc) port = std::stoi(argv[++i]);
      } else if (arg == "--host") {
        if (i + 1 < argc) host = argv[++i];
      } else if (arg == "--device" || arg == "-d") {
        if (i + 1 < argc) device = argv[++i];
      } else if (arg == "--asr-model") {
        if (i + 1 < argc) asr_model = argv[++i];
      } else if (arg == "--vlm-model") {
        if (i + 1 < argc) vlm_model = argv[++i];
      } else if (arg == "--tts-model") {
        if (i + 1 < argc) tts_model = argv[++i];
      }
    }

    // Auto-resolve model paths from cache
    ModelManager manager;
    try {
      if (asr_model.empty()) asr_model = manager.GetModelPath("nemotron");
    } catch (...) {}
    try {
      if (vlm_model.empty()) vlm_model = manager.GetModelPath("qwen3-vl");
    } catch (...) {}
    try {
      if (tts_model.empty()) tts_model = manager.GetModelPath("kokoro");
    } catch (...) {}

    EDGESCRIBE::server::ServerConfig config;
    config.port = port;
    config.host = host;
    config.device = device;
    config.asr_model = asr_model;
    config.vlm_model = vlm_model;
    config.tts_model = tts_model;

    // Handle Ctrl+C
    std::signal(SIGINT, SignalHandler);
#ifdef _WIN32
    std::signal(SIGBREAK, SignalHandler);
#endif

    try {
      EDGESCRIBE::server::ApiServer server(config);

      // Stop on Ctrl+C in a separate thread
      std::thread stop_thread([&]() {
        while (g_running.load()) {
          std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        server.Stop();
      });
      stop_thread.detach();

      server.Start();
      return 0;
    } catch (const std::exception& e) {
      std::cerr << "Error: " << e.what() << std::endl;
      return 1;
    }
  }

  std::cerr << "Unknown command: " << command << std::endl;
  std::cerr << "Run 'EDGESCRIBE --help' for usage." << std::endl;
  return 1;
}
