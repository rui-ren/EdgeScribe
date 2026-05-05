// EDGESCRIBE — On-device AI assistant. Speech, vision, language.
//
// Usage:
//   edgescribe pull nemotron              Download a model
//   edgescribe run --live                 Live microphone transcription
//   edgescribe run meeting.wav            Transcribe a WAV file
//   edgescribe chat "prompt"              Chat with local LLM
//   edgescribe vision image.jpg           Analyze image / OCR
//   edgescribe process --soap file.txt    Generate SOAP notes
//   edgescribe list                       List available/downloaded models

#include "asr/audio_capture.h"
#include "asr/audio_file.h"
#include "core/model_manager.h"
#include "core/memory_store.h"
#include "asr/transcriber.h"
#include "asr/diarizer.h"
#include "llm/llm_engine.h"
#include "vision/vision_engine.h"
#include "tts/tts_engine.h"
#include "server/api_server.h"

#ifdef EDGESCRIBE_HAS_GUI
#include "gui/gui_launcher.h"
#endif

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
  std::cout << "Model Management:" << std::endl;
  std::cout << "  edgescribe model list                 List available and downloaded models" << std::endl;
  std::cout << "  edgescribe model pull <name>          Download a model from HuggingFace" << std::endl;
  std::cout << "  edgescribe model remove <name>        Delete a downloaded model" << std::endl;
  std::cout << "  edgescribe model cache                Show cache directory and disk usage" << std::endl;
  std::cout << std::endl;
  std::cout << "Speech-to-Text (ASR):" << std::endl;
  std::cout << "  edgescribe run --live [options]       Live microphone transcription" << std::endl;
  std::cout << "  edgescribe run <file.wav> [options]   Transcribe a WAV file" << std::endl;
  std::cout << "  edgescribe run <file.wav> --diarize   Transcribe with speaker labels" << std::endl;
  std::cout << "  edgescribe devices                    List audio input devices" << std::endl;
  std::cout << std::endl;
  std::cout << "Vision & Language:" << std::endl;
  std::cout << "  edgescribe vision <image> [--prompt]  Analyze image (OCR, understanding)" << std::endl;
  std::cout << "  edgescribe chat <prompt>              Chat with local LLM (single-turn)" << std::endl;
  std::cout << "  edgescribe chat                       Interactive multi-turn chat" << std::endl;
  std::cout << "  edgescribe process --soap <file>      Generate SOAP notes from transcript" << std::endl;
  std::cout << std::endl;
  std::cout << "History:" << std::endl;
  std::cout << "  edgescribe history                    List recent sessions" << std::endl;
  std::cout << "  edgescribe history show <id>          Show session messages" << std::endl;
  std::cout << "  edgescribe history search \"query\"     Search past conversations" << std::endl;
  std::cout << "  edgescribe history delete <id>        Delete a session" << std::endl;
  std::cout << std::endl;
  std::cout << "Text-to-Speech:" << std::endl;
  std::cout << "  edgescribe speak <text|file>          Read text aloud" << std::endl;
  std::cout << std::endl;
  std::cout << "Server:" << std::endl;
  std::cout << "  edgescribe serve [--port 8080]        Start REST API server" << std::endl;
#ifdef EDGESCRIBE_HAS_GUI
  std::cout << "  edgescribe gui   [--port 8080]        Open native desktop app" << std::endl;
#endif
  std::cout << std::endl;
  std::cout << "Options:" << std::endl;
  std::cout << "  --model <name|path>    Model to use (default: nemotron for ASR)" << std::endl;
  std::cout << "  --device <ep>          Device: cpu, cuda, metal, vulkan, rocm" << std::endl;
  std::cout << "  -o <file>              Write output to file" << std::endl;
  std::cout << "  --version              Show version" << std::endl;
  std::cout << "  --help                 Show this help" << std::endl;
  std::cout << std::endl;
  std::cout << "Examples:" << std::endl;
  std::cout << "  edgescribe pull nemotron                 # Download ASR model (~670 MB)" << std::endl;
  std::cout << "  edgescribe pull qwen3-vl                 # Download vision+LLM (~990 MB)" << std::endl;
  std::cout << "  edgescribe pull kokoro                   # Download TTS (~300 MB)" << std::endl;
  std::cout << "  edgescribe run --live                    # Live transcription" << std::endl;
  std::cout << "  edgescribe run meeting.wav -o notes.txt  # File transcription" << std::endl;
}

// ── Command: pull ──────────────────────────────────────────────────────────
static int CmdPull(const std::string& model_name, const std::string& token = "") {
  try {
    ModelManager manager;
    manager.Pull(model_name, token);
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
  const char* types[] = {"asr", "vlm", "tts"};
  const char* labels[] = {
      "Speech-to-Text (ASR)",
      "Vision + Language (VLM)",
      "Text-to-Speech (TTS)"
  };

  for (int t = 0; t < 3; t++) {
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

// ── Command: model cache ──────────────────────────────────────────────────
static int CmdModelCache() {
  ModelManager manager;
  auto cached = manager.ListCached();

  std::cout << "Cache directory: " << manager.GetCacheDir() << std::endl;
  std::cout << std::endl;

  if (cached.empty()) {
    std::cout << "No models downloaded." << std::endl;
    return 0;
  }

  size_t total_mb = 0;
  std::cout << std::left
            << std::setw(16) << "Model"
            << std::setw(12) << "Type"
            << std::setw(12) << "Size"
            << std::endl;
  std::cout << std::string(40, '-') << std::endl;

  for (const auto& m : cached) {
    std::cout << std::setw(16) << m.name
              << std::setw(12) << m.type
              << "~" << m.size_mb << " MB"
              << std::endl;
    total_mb += m.size_mb;
  }

  std::cout << std::string(40, '-') << std::endl;
  std::cout << "Total: " << cached.size() << " model(s), ~" << total_mb << " MB" << std::endl;

  return 0;
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
  // ── model (parent command) ──
  if (command == "model") {
    std::string subcmd;
    if (argc >= 3) subcmd = argv[2];

    if (subcmd == "list" || subcmd == "ls") {
      return CmdList();
    }
    if (subcmd == "pull" || subcmd == "download") {
      if (argc < 4) {
        std::cerr << "Usage: edgescribe model pull <name> [--token <hf_token>]" << std::endl;
        return 1;
      }
      std::string pull_token;
      for (int i = 4; i < argc; i++) {
        std::string arg = argv[i];
        if ((arg == "--token" || arg == "-t") && i + 1 < argc) {
          pull_token = argv[++i];
        }
      }
      return CmdPull(argv[3], pull_token);
    }
    if (subcmd == "remove" || subcmd == "rm" || subcmd == "delete") {
      if (argc < 4) {
        std::cerr << "Usage: edgescribe model remove <name>" << std::endl;
        return 1;
      }
      return CmdRemove(argv[3]);
    }
    if (subcmd == "cache") {
      return CmdModelCache();
    }

    // No subcommand or unknown — show model help
    std::cout << "Model management commands:" << std::endl;
    std::cout << "  edgescribe model list                 List available and downloaded models" << std::endl;
    std::cout << "  edgescribe model pull <name>          Download a model" << std::endl;
    std::cout << "  edgescribe model remove <name>        Delete a downloaded model" << std::endl;
    std::cout << "  edgescribe model cache                Show cache directory and disk usage" << std::endl;
    return 0;
  }

  // ── Backward-compatible aliases ──
  if (command == "pull") {
    if (argc < 3) {
      std::cerr << "Usage: edgescribe model pull <name> [--token <hf_token>]" << std::endl;
      return 1;
    }
    std::string pull_model = argv[2];
    std::string pull_token;
    for (int i = 3; i < argc; i++) {
      std::string arg = argv[i];
      if ((arg == "--token" || arg == "-t") && i + 1 < argc) {
        pull_token = argv[++i];
      }
    }
    return CmdPull(pull_model, pull_token);
  }

  if (command == "list") {
    return CmdList();
  }

  if (command == "remove" || command == "rm") {
    if (argc < 3) {
      std::cerr << "Usage: edgescribe model remove <name>" << std::endl;
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
    bool interactive = false;

    for (int i = 2; i < argc; i++) {
      std::string arg = argv[i];
      if (arg == "--model" || arg == "-m") {
        if (i + 1 < argc) model_name = argv[++i];
      } else if (arg == "-o" || arg == "--output") {
        if (i + 1 < argc) output_file = argv[++i];
      } else if (arg == "--device" || arg == "-d") {
        if (i + 1 < argc) device = argv[++i];
      } else if (arg == "-i" || arg == "--interactive") {
        interactive = true;
      } else if (arg[0] != '-') {
        if (!prompt.empty()) prompt += " ";
        prompt += arg;
      }
    }

    // If no prompt given, enter interactive mode
    if (prompt.empty()) {
      interactive = true;
    }

    try {
      ModelManager manager;
      std::string model_path = manager.GetModelPath(model_name);

      std::cout << "Loading model" << (device != "cpu" ? " (" + device + ")" : "") << "..." << std::endl;
      EDGESCRIBE::llm::LlmEngine engine(model_path, device);

      if (interactive) {
        // Multi-turn interactive chat
        std::vector<EDGESCRIBE::llm::ChatMessage> history;
        history.push_back({"system", "You are a helpful assistant."});

        // Auto-save: start a session
        EDGESCRIBE::MemoryStore memory(EDGESCRIBE::GetDefaultDbPath());
        std::string session_id = memory.StartSession("chat", model_name);

        std::cout << "Chat started. Type your message and press Enter. "
                  << "Type /exit to quit, /clear to reset." << std::endl;
        std::cout << std::string(60, '-') << std::endl;

        while (g_running.load()) {
          std::cout << "\nYou: ";
          std::string user_input;
          if (!std::getline(std::cin, user_input)) break;
          if (user_input.empty()) continue;

          if (user_input == "/exit" || user_input == "/quit") break;
          if (user_input == "/clear") {
            history.clear();
            history.push_back({"system", "You are a helpful assistant."});
            // Start a new session for the cleared conversation
            memory.EndSession(session_id);
            session_id = memory.StartSession("chat", model_name);
            std::cout << "Chat history cleared." << std::endl;
            continue;
          }

          history.push_back({"user", user_input});
          memory.SaveMessage(session_id, "user", user_input);

          std::cout << "\nAssistant: ";
          std::string result = engine.Chat(
              history, 2048,
              [](const std::string& token) { std::cout << token << std::flush; });
          std::cout << std::endl;

          history.push_back({"assistant", result});
          memory.SaveMessage(session_id, "assistant", result);
        }

        memory.EndSession(session_id);

        // Save full conversation if output file specified
        if (!output_file.empty()) {
          std::ofstream ofs(output_file);
          if (ofs.is_open()) {
            for (const auto& msg : history) {
              if (msg.role == "system") continue;
              std::string label = (msg.role == "user") ? "You" : "Assistant";
              ofs << label << ": " << msg.content << "\n\n";
            }
            std::cout << "Conversation saved to: " << output_file << std::endl;
          }
        }
      } else {
        // Single-turn chat (original behavior)
        std::cout << std::string(60, '-') << std::endl;
        std::string result = engine.Chat(
            "You are a helpful assistant.",
            prompt,
            2048,
            [](const std::string& token) { std::cout << token << std::flush; });

        std::cout << std::endl;

        // Auto-save single-turn chat
        try {
          EDGESCRIBE::MemoryStore memory(EDGESCRIBE::GetDefaultDbPath());
          std::string sid = memory.StartSession("chat", model_name);
          memory.SaveMessage(sid, "user", prompt);
          memory.SaveMessage(sid, "assistant", result);
          memory.EndSession(sid);
        } catch (...) { /* Don't fail on save errors */ }

        if (!output_file.empty()) {
          std::ofstream ofs(output_file);
          if (ofs.is_open()) {
            ofs << result << std::endl;
            std::cout << "Saved to: " << output_file << std::endl;
          }
        }
      }
      return 0;
    } catch (const std::exception& e) {
      std::cerr << "Error: " << e.what() << std::endl;
      return 1;
    }
  }

  // ── history ──
  if (command == "history") {
    std::string subcmd;
    std::string arg;

    if (argc >= 3) subcmd = argv[2];
    if (argc >= 4) arg = argv[3];

    try {
      EDGESCRIBE::MemoryStore memory(EDGESCRIBE::GetDefaultDbPath());

      if (subcmd.empty() || subcmd == "list") {
        // List recent sessions
        auto sessions = memory.GetRecentSessions(20);
        if (sessions.empty()) {
          std::cout << "No sessions found." << std::endl;
          return 0;
        }
        std::cout << std::left
                  << std::setw(12) << "ID"
                  << std::setw(12) << "Type"
                  << std::setw(22) << "Started"
                  << std::setw(8) << "Msgs"
                  << std::endl;
        std::cout << std::string(54, '-') << std::endl;
        for (const auto& s : sessions) {
          std::cout << std::setw(12) << s.id
                    << std::setw(12) << s.type
                    << std::setw(22) << s.started_at
                    << std::setw(8) << s.message_count
                    << std::endl;
        }
      } else if (subcmd == "show") {
        if (arg.empty()) {
          std::cerr << "Usage: edgescribe history show <session_id>" << std::endl;
          return 1;
        }
        auto messages = memory.GetMessages(arg);
        auto notes = memory.GetNotes(arg);

        if (messages.empty() && notes.empty()) {
          std::cout << "No data found for session: " << arg << std::endl;
          return 0;
        }

        for (const auto& msg : messages) {
          if (msg.role == "system") continue;
          std::string label = (msg.role == "user") ? "You" : "Assistant";
          std::cout << label << ": " << msg.content << "\n" << std::endl;
        }
        for (const auto& note : notes) {
          std::cout << "[" << note.type << " output]" << std::endl;
          std::cout << note.output_text << std::endl;
        }
      } else if (subcmd == "search") {
        if (arg.empty()) {
          std::cerr << "Usage: edgescribe history search \"query\"" << std::endl;
          return 1;
        }
        // Collect remaining args as query
        std::string query;
        for (int i = 3; i < argc; i++) {
          if (!query.empty()) query += " ";
          query += argv[i];
        }
        auto results = memory.SearchMessages(query, 20);
        if (results.empty()) {
          std::cout << "No results found for: " << query << std::endl;
          return 0;
        }
        for (const auto& msg : results) {
          std::cout << "[" << msg.session_id << " " << msg.created_at << "] "
                    << msg.role << ": "
                    << msg.content.substr(0, 120)
                    << (msg.content.size() > 120 ? "..." : "")
                    << std::endl;
        }
      } else if (subcmd == "delete") {
        if (arg.empty()) {
          std::cerr << "Usage: edgescribe history delete <session_id>" << std::endl;
          return 1;
        }
        memory.DeleteSession(arg);
        std::cout << "Session " << arg << " deleted." << std::endl;
      } else {
        std::cerr << "Unknown subcommand: " << subcmd << std::endl;
        std::cerr << "Usage: edgescribe history [list|show|search|delete]" << std::endl;
        return 1;
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
      std::cerr << "Usage: edgescribe vision <image.jpg> [--prompt \"...\"]" << std::endl;
      std::cerr << "       edgescribe vision <image.jpg> --ocr" << std::endl;
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
      std::cerr << "Usage: edgescribe process --soap <transcript.txt>" << std::endl;
      std::cerr << "       edgescribe process --summarize <file.txt>" << std::endl;
      std::cerr << "       edgescribe process --fix-terms <transcript.txt>" << std::endl;
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

      // Auto-save process result
      try {
        std::string note_type = soap_mode ? "soap" : summarize_mode ? "summary" : "fix-terms";
        EDGESCRIBE::MemoryStore memory(EDGESCRIBE::GetDefaultDbPath());
        std::string sid = memory.StartSession("process", model_name);
        memory.SaveNote(sid, note_type, content, result);
        memory.EndSession(sid);
      } catch (...) { /* Don't fail on save errors */ }

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

    if (live_mode) {
      return CmdRunLive(model_path, output_file, device);
    } else if (!audio_file.empty()) {
      int rc = CmdRunFile(audio_file, model_path, output_file, device, srt_file, vtt_file);

      // Post-process with diarization if requested
      if (rc == 0 && diarize) {
        try {
          std::string diar_model_path = manager.GetModelPath("ecapa-tdnn");
          auto audio = LoadWavFile(audio_file);

          std::cout << "\nRunning speaker diarization..." << std::endl;
          Transcriber transcriber(model_path, device);

          constexpr size_t kDiarChunkSize = 8960;
          for (size_t i = 0; i < audio.samples.size(); i += kDiarChunkSize) {
            size_t remaining = std::min(kDiarChunkSize, audio.samples.size() - i);
            transcriber.ProcessChunk(audio.samples.data() + i, remaining);
          }
          transcriber.Flush();

          auto words = transcriber.GetTimestampedWords();

          Diarizer diarizer(diar_model_path + "/model.onnx", device);
          auto segments = diarizer.Diarize(
              audio.samples.data(), audio.samples.size(), 16000, words);

          std::cout << std::string(60, '-') << std::endl;
          std::cout << FormatDiarizedTranscript(segments);
          std::cout << std::string(60, '-') << std::endl;

          int num_speakers = 0;
          for (const auto& seg : segments) {
            num_speakers = std::max(num_speakers, seg.speaker_id + 1);
          }
          std::cout << "  Speakers detected: " << num_speakers << std::endl;

          if (!output_file.empty()) {
            std::string diar_file = output_file;
            auto dot = diar_file.rfind('.');
            if (dot != std::string::npos) {
              diar_file = diar_file.substr(0, dot) + ".diarized" + diar_file.substr(dot);
            } else {
              diar_file += ".diarized.txt";
            }
            std::ofstream ofs(diar_file);
            if (ofs.is_open()) {
              ofs << FormatDiarizedTranscript(segments);
              std::cout << "  Diarized output saved to: " << diar_file << std::endl;
            }
          }
        } catch (const std::exception& e) {
          std::cerr << "Diarization error: " << e.what() << std::endl;
        }
      }

      return rc;
    } else {
      std::cerr << "Usage: edgescribe run --live" << std::endl;
      std::cerr << "       edgescribe run <file.wav>" << std::endl;
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
      std::cerr << "Usage: edgescribe speak \"Hello world\"" << std::endl;
      std::cerr << "       edgescribe speak notes.txt" << std::endl;
      std::cerr << "       edgescribe speak \"Hello\" -o output.wav" << std::endl;
      std::cerr << "       edgescribe speak --voices" << std::endl;
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

  // ── gui ──
#ifdef EDGESCRIBE_HAS_GUI
  if (command == "gui") {
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
    try { if (asr_model.empty()) asr_model = manager.GetModelPath("nemotron"); } catch (...) {}
    try { if (vlm_model.empty()) vlm_model = manager.GetModelPath("qwen3-vl"); } catch (...) {}
    try { if (tts_model.empty()) tts_model = manager.GetModelPath("kokoro"); } catch (...) {}

    EDGESCRIBE::gui::GuiConfig gui_config;
    gui_config.server_config.port = port;
    gui_config.server_config.host = host;
    gui_config.server_config.device = device;
    gui_config.server_config.asr_model = asr_model;
    gui_config.server_config.vlm_model = vlm_model;
    gui_config.server_config.tts_model = tts_model;

    try {
      EDGESCRIBE::gui::Launch(gui_config);
      return 0;
    } catch (const std::exception& e) {
      std::cerr << "Error: " << e.what() << std::endl;
      return 1;
    }
  }
#endif

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
  std::cerr << "Run 'edgescribe --help' for usage." << std::endl;
  return 1;
}
