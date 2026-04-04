// EDGESCRIBE — REST API Server Implementation
// Provides HTTP endpoints for all EDGESCRIBE engines
//
// Endpoints:
//   GET  /v1/health                    — Health check
//   GET  /v1/models                    — List loaded models
//
//   POST /v1/transcribe/file           — Transcribe uploaded WAV file
//   POST /v1/transcribe/stream         — Start streaming ASR session (SSE)
//
//   POST /v1/chat                      — LLM chat completion
//   POST /v1/chat/soap                 — Generate SOAP notes
//   POST /v1/chat/summarize            — Summarize text
//   POST /v1/chat/fix-terms            — Fix medical terminology
//
//   POST /v1/vision/analyze            — Analyze image with prompt
//   POST /v1/vision/ocr               — OCR an image
//
//   POST /v1/tts/synthesize            — Synthesize text to audio (returns WAV)
//   GET  /v1/tts/voices                — List available TTS voices

#include "server/api_server.h"
#include "httplib.h"

#include "asr/transcriber.h"
#include "asr/audio_file.h"
#include "llm/llm_engine.h"
#include "vision/vision_engine.h"
#include "tts/tts_engine.h"
#include "core/memory_store.h"

#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <mutex>
#include <sstream>
#include <thread>

namespace fs = std::filesystem;

namespace EDGESCRIBE::server {

// ── Simple JSON helpers (avoid full JSON library dependency) ─────────────────
static std::string JsonEscape(const std::string& s) {
  std::string result;
  result.reserve(s.size() + 16);
  for (char c : s) {
    switch (c) {
      case '"':  result += "\\\""; break;
      case '\\': result += "\\\\"; break;
      case '\n': result += "\\n"; break;
      case '\r': result += "\\r"; break;
      case '\t': result += "\\t"; break;
      default:   result += c; break;
    }
  }
  return result;
}

static std::string JsonObj(
    std::initializer_list<std::pair<std::string, std::string>> fields) {
  std::ostringstream ss;
  ss << "{";
  bool first = true;
  for (const auto& [key, value] : fields) {
    if (!first) ss << ",";
    ss << "\"" << key << "\":" << value;
    first = false;
  }
  ss << "}";
  return ss.str();
}

static std::string JsonStr(const std::string& s) {
  return "\"" + JsonEscape(s) + "\"";
}

static std::string JsonNum(double n) {
  std::ostringstream ss;
  ss << n;
  return ss.str();
}

static std::string JsonBool(bool b) {
  return b ? "true" : "false";
}

// Extract a string field from a JSON-like body (simple parser)
static std::string ExtractJsonString(const std::string& body,
                                     const std::string& key) {
  std::string search = "\"" + key + "\"";
  auto pos = body.find(search);
  if (pos == std::string::npos) return "";

  pos = body.find(':', pos + search.size());
  if (pos == std::string::npos) return "";

  pos = body.find('"', pos + 1);
  if (pos == std::string::npos) return "";

  auto end = pos + 1;
  while (end < body.size()) {
    if (body[end] == '"' && body[end - 1] != '\\') break;
    end++;
  }

  return body.substr(pos + 1, end - pos - 1);
}

static double ExtractJsonNumber(const std::string& body,
                                const std::string& key,
                                double default_val) {
  std::string search = "\"" + key + "\"";
  auto pos = body.find(search);
  if (pos == std::string::npos) return default_val;

  pos = body.find(':', pos + search.size());
  if (pos == std::string::npos) return default_val;

  pos++;
  while (pos < body.size() && body[pos] == ' ') pos++;

  try {
    return std::stod(body.substr(pos));
  } catch (...) {
    return default_val;
  }
}

// Extract a JSON array of message objects: [{"role":"...","content":"..."},...]
static std::vector<EDGESCRIBE::llm::ChatMessage>
ExtractJsonMessages(const std::string& body) {
  std::vector<EDGESCRIBE::llm::ChatMessage> messages;

  std::string search = "\"messages\"";
  auto pos = body.find(search);
  if (pos == std::string::npos) return messages;

  pos = body.find('[', pos);
  if (pos == std::string::npos) return messages;

  // Walk through the array finding each {role, content} object
  size_t i = pos + 1;
  while (i < body.size()) {
    auto obj_start = body.find('{', i);
    if (obj_start == std::string::npos) break;

    auto obj_end = body.find('}', obj_start);
    if (obj_end == std::string::npos) break;

    std::string obj = body.substr(obj_start, obj_end - obj_start + 1);
    std::string role = ExtractJsonString(obj, "role");
    std::string content = ExtractJsonString(obj, "content");

    if (!role.empty() && !content.empty()) {
      messages.push_back({role, content});
    }

    i = obj_end + 1;
    if (body.find(']', i) < body.find('{', i)) break;
  }

  return messages;
}
struct ApiServer::Impl {
  httplib::Server svr;
  ServerConfig config;

  // LLM/Vision: loaded at startup via mmap — OS manages physical RAM
  // ASR/TTS: lazy-loaded on first request — full RAM load, unloaded when idle
  std::unique_ptr<Transcriber> transcriber;
  std::unique_ptr<llm::LlmEngine> llm;
  std::unique_ptr<vision::VisionEngine> vision;
  std::unique_ptr<tts::TtsEngine> tts;

  // Persistent memory store
  std::unique_ptr<MemoryStore> memory;

  std::mutex asr_mutex;
  std::mutex llm_mutex;
  std::mutex vision_mutex;
  std::mutex tts_mutex;

  // Idle tracking for ONNX engines only (ASR/TTS)
  std::chrono::steady_clock::time_point asr_last_used;
  std::chrono::steady_clock::time_point tts_last_used;

  static constexpr int kIdleTimeoutSeconds = 300;  // Unload ONNX engines after 5 min idle
  std::atomic<bool> running{false};
  std::thread idle_monitor_thread;

  // Lazy-load helpers for ONNX engines
  bool EnsureASR(httplib::Response& res);
  bool EnsureLLM(httplib::Response& res);
  bool EnsureVision(httplib::Response& res);
  bool EnsureTTS(httplib::Response& res);

  void InitEngines();
  void RegisterRoutes();
  void StartIdleMonitor();
  void StopIdleMonitor();
};

void ApiServer::Impl::InitEngines() {
  // LLM/Vision: load eagerly — llama.cpp uses mmap, so virtual memory is
  // allocated but physical RAM is only used as pages are accessed. Idle cost
  // is near zero. This avoids cold-start latency on first chat/vision request.
  if (!config.vlm_model.empty() && fs::exists(config.vlm_model)) {
    try {
      std::cout << "  Loading VLM/LLM model (mmap)..." << std::endl;
      llm = std::make_unique<llm::LlmEngine>(config.vlm_model, config.device);
      vision = std::make_unique<vision::VisionEngine>(config.vlm_model, config.device);
      std::cout << "  VLM/LLM ready." << std::endl;
    } catch (const std::exception& e) {
      std::cerr << "  Warning: Failed to load VLM/LLM: " << e.what() << std::endl;
    }
  }

  // ASR/TTS: lazy-loaded on first request. ONNX Runtime fully loads models
  // into RAM (~670 MB for ASR, ~300 MB for TTS), so we defer until needed
  // and unload after idle timeout to free memory.
  if (!config.asr_model.empty() && fs::exists(config.asr_model)) {
    std::cout << "  ASR model ready (will load on first request)" << std::endl;
  }
  if (!config.tts_model.empty() && fs::exists(config.tts_model)) {
    std::cout << "  TTS model ready (will load on first request)" << std::endl;
  }

  // Initialize memory store
  try {
    memory = std::make_unique<MemoryStore>(GetDefaultDbPath());
    std::cout << "  Memory store ready." << std::endl;
  } catch (const std::exception& e) {
    std::cerr << "  Warning: Memory store failed: " << e.what() << std::endl;
  }

  StartIdleMonitor();
}

bool ApiServer::Impl::EnsureASR(httplib::Response& res) {
  std::lock_guard<std::mutex> lock(asr_mutex);
  asr_last_used = std::chrono::steady_clock::now();
  if (transcriber) return true;

  if (config.asr_model.empty() || !fs::exists(config.asr_model)) {
    res.status = 503;
    res.set_content(JsonObj({{"error", JsonStr("ASR model not configured")}}),
                    "application/json");
    return false;
  }

  try {
    std::cout << "  [lazy] Loading ASR model..." << std::endl;
    transcriber = std::make_unique<Transcriber>(config.asr_model, config.device);
    std::cout << "  [lazy] ASR model loaded." << std::endl;
    return true;
  } catch (const std::exception& e) {
    res.status = 500;
    res.set_content(JsonObj({{"error", JsonStr(std::string("Failed to load ASR: ") + e.what())}}),
                    "application/json");
    return false;
  }
}

bool ApiServer::Impl::EnsureLLM(httplib::Response& res) {
  // LLM is eagerly loaded at startup (mmap). Just check availability.
  if (llm) return true;
  res.status = 503;
  res.set_content(JsonObj({{"error", JsonStr("LLM model not loaded")}}),
                  "application/json");
  return false;
}

bool ApiServer::Impl::EnsureVision(httplib::Response& res) {
  // Vision is eagerly loaded at startup (mmap). Just check availability.
  if (vision) return true;
  res.status = 503;
  res.set_content(JsonObj({{"error", JsonStr("Vision model not loaded")}}),
                  "application/json");
  return false;
}

bool ApiServer::Impl::EnsureTTS(httplib::Response& res) {
  std::lock_guard<std::mutex> lock(tts_mutex);
  tts_last_used = std::chrono::steady_clock::now();
  if (tts) return true;

  if (config.tts_model.empty() || !fs::exists(config.tts_model)) {
    res.status = 503;
    res.set_content(JsonObj({{"error", JsonStr("TTS model not configured")}}),
                    "application/json");
    return false;
  }

  try {
    std::cout << "  [lazy] Loading TTS model..." << std::endl;
    tts = std::make_unique<tts::TtsEngine>(config.tts_model);
    std::cout << "  [lazy] TTS model loaded." << std::endl;
    return true;
  } catch (const std::exception& e) {
    res.status = 500;
    res.set_content(JsonObj({{"error", JsonStr(std::string("Failed to load TTS: ") + e.what())}}),
                    "application/json");
    return false;
  }
}

void ApiServer::Impl::StartIdleMonitor() {
  idle_monitor_thread = std::thread([this]() {
    while (running) {
      std::this_thread::sleep_for(std::chrono::seconds(30));
      if (!running) break;

      auto now = std::chrono::steady_clock::now();
      auto timeout = std::chrono::seconds(kIdleTimeoutSeconds);

      // Only unload ONNX engines (ASR/TTS) — they fully load into RAM.
      // LLM/Vision use mmap via llama.cpp, so the OS manages physical RAM.

      {
        std::lock_guard<std::mutex> lock(asr_mutex);
        if (transcriber && (now - asr_last_used) > timeout) {
          std::cout << "  [idle] Unloading ASR model (idle > "
                    << kIdleTimeoutSeconds << "s, frees ~1.2 GB)" << std::endl;
          transcriber.reset();
        }
      }

      {
        std::lock_guard<std::mutex> lock(tts_mutex);
        if (tts && (now - tts_last_used) > timeout) {
          std::cout << "  [idle] Unloading TTS model (idle > "
                    << kIdleTimeoutSeconds << "s, frees ~700 MB)" << std::endl;
          tts.reset();
        }
      }
    }
  });
}

void ApiServer::Impl::StopIdleMonitor() {
  running = false;
  if (idle_monitor_thread.joinable()) {
    idle_monitor_thread.join();
  }
}

void ApiServer::Impl::RegisterRoutes() {
  // ── CORS headers for frontend access ──
  svr.set_pre_routing_handler(
      [](const httplib::Request&, httplib::Response& res) {
        res.set_header("Access-Control-Allow-Origin", "*");
        res.set_header("Access-Control-Allow-Methods",
                       "GET, POST, OPTIONS");
        res.set_header("Access-Control-Allow-Headers",
                       "Content-Type, Authorization");
        return httplib::Server::HandlerResponse::Unhandled;
      });

  // Handle CORS preflight
  svr.Options("/(.*)", [](const httplib::Request&, httplib::Response& res) {
    res.set_header("Access-Control-Allow-Origin", "*");
    res.set_header("Access-Control-Allow-Methods", "GET, POST, OPTIONS");
    res.set_header("Access-Control-Allow-Headers",
                   "Content-Type, Authorization");
    res.status = 204;
  });

  // ── GET /v1/health ──
  svr.Get("/v1/health", [this](const httplib::Request&, httplib::Response& res) {
    // For lazy-loaded engines (ASR, TTS), report "available" if the model
    // path is configured — not just if currently loaded in memory.
    bool asr_available = transcriber != nullptr ||
        (!config.asr_model.empty() && fs::exists(config.asr_model));
    bool tts_available = tts != nullptr ||
        (!config.tts_model.empty() && fs::exists(config.tts_model));

    auto body = JsonObj({
        {"status", JsonStr("ok")},
        {"version", JsonStr("0.1.0")},
        {"engines", JsonObj({
            {"asr", JsonBool(asr_available)},
            {"llm", JsonBool(llm != nullptr)},
            {"vision", JsonBool(vision != nullptr)},
            {"tts", JsonBool(tts_available)},
        })},
    });
    res.set_content(body, "application/json");
  });

  // ── GET /v1/models ──
  svr.Get("/v1/models",
          [this](const httplib::Request&, httplib::Response& res) {
    std::ostringstream ss;
    ss << "{\"models\":[";
    bool first = true;

    if (transcriber) {
      ss << JsonObj({{"name", JsonStr("nemotron")},
                     {"type", JsonStr("asr")},
                     {"loaded", JsonBool(true)}});
      first = false;
    }
    if (llm) {
      if (!first) ss << ",";
      ss << JsonObj({{"name", JsonStr("qwen3-vl")},
                     {"type", JsonStr("llm")},
                     {"loaded", JsonBool(true)}});
      first = false;
    }
    if (vision) {
      if (!first) ss << ",";
      ss << JsonObj({{"name", JsonStr("qwen3-vl")},
                     {"type", JsonStr("vlm")},
                     {"loaded", JsonBool(true)}});
      first = false;
    }
    if (tts) {
      if (!first) ss << ",";
      ss << JsonObj({{"name", JsonStr("kokoro")},
                     {"type", JsonStr("tts")},
                     {"loaded", JsonBool(true)}});
    }
    ss << "]}";
    res.set_content(ss.str(), "application/json");
  });

  // ── POST /v1/transcribe/file ──
  // Body: multipart form with "audio" file field
  // Response: { "text": "...", "duration": 12.3 }
  svr.Post("/v1/transcribe/file",
           [this](const httplib::Request& req, httplib::Response& res) {
    if (!EnsureASR(res)) return;

    // Check for multipart file
    if (!req.form.has_file("audio")) {
      res.status = 400;
      res.set_content(
          JsonObj({{"error", JsonStr("Missing 'audio' file in multipart form")}}),
          "application/json");
      return;
    }

    auto file = req.form.get_file("audio");

    // Write to temp file for miniaudio to decode
    auto temp_path = fs::temp_directory_path() / "EDGESCRIBE_upload.wav";
    {
      std::ofstream ofs(temp_path, std::ios::binary);
      ofs.write(file.content.data(),
                static_cast<std::streamsize>(file.content.size()));
    }

    try {
      auto audio = LoadWavFile(temp_path.string());
      fs::remove(temp_path);

      std::lock_guard<std::mutex> lock(asr_mutex);
      transcriber->Reset();

      constexpr size_t kChunkSize = 8960;
      for (size_t i = 0; i < audio.samples.size(); i += kChunkSize) {
        size_t remaining = std::min(kChunkSize, audio.samples.size() - i);
        transcriber->ProcessChunk(audio.samples.data() + i, remaining);
      }
      transcriber->Flush();

      auto body = JsonObj({
          {"text", JsonStr(transcriber->GetTranscript())},
          {"duration", JsonNum(audio.duration_seconds)},
      });
      res.set_content(body, "application/json");
    } catch (const std::exception& e) {
      fs::remove(temp_path);
      res.status = 500;
      res.set_content(JsonObj({{"error", JsonStr(e.what())}}),
                      "application/json");
    }
  });

  // ── POST /v1/transcribe/stream ──
  // Body: { } (empty, starts SSE stream)
  // Response: Server-Sent Events with partial transcripts
  // Client should POST audio chunks to /v1/transcribe/push after starting
  svr.Post("/v1/transcribe/stream",
           [this](const httplib::Request&, httplib::Response& res) {
    if (!EnsureASR(res)) return;

    std::lock_guard<std::mutex> lock(asr_mutex);
    transcriber->Reset();

    auto body = JsonObj({
        {"status", JsonStr("streaming_ready")},
        {"message", JsonStr("POST audio chunks to /v1/transcribe/push as raw PCM (16kHz, float32, mono)")},
    });
    res.set_content(body, "application/json");
  });

  // ── POST /v1/transcribe/push ──
  // Body: raw PCM float32 audio bytes
  // Response: { "text": "partial transcript", "is_final": false }
  svr.Post("/v1/transcribe/push",
           [this](const httplib::Request& req, httplib::Response& res) {
    if (!EnsureASR(res)) return;

    if (req.body.empty()) {
      res.status = 400;
      res.set_content(JsonObj({{"error", JsonStr("Empty audio data")}}),
                      "application/json");
      return;
    }

    std::lock_guard<std::mutex> lock(asr_mutex);

    const float* samples = reinterpret_cast<const float*>(req.body.data());
    size_t count = req.body.size() / sizeof(float);
    std::string text = transcriber->ProcessChunk(samples, count);

    auto body = JsonObj({
        {"text", JsonStr(text)},
        {"is_final", JsonBool(false)},
        {"transcript", JsonStr(transcriber->GetTranscript())},
    });
    res.set_content(body, "application/json");
  });

  // ── POST /v1/transcribe/flush ──
  // Flush remaining audio and get final transcript
  svr.Post("/v1/transcribe/flush",
           [this](const httplib::Request&, httplib::Response& res) {
    if (!EnsureASR(res)) return;

    std::lock_guard<std::mutex> lock(asr_mutex);
    std::string final_text = transcriber->Flush();

    auto body = JsonObj({
        {"text", JsonStr(final_text)},
        {"is_final", JsonBool(true)},
        {"transcript", JsonStr(transcriber->GetTranscript())},
    });
    res.set_content(body, "application/json");
  });

  // ── POST /v1/chat ──
  // Body: { "prompt": "...", "system": "...", "max_length": 2048 }
  // Response: { "text": "..." }
  // ── POST /v1/chat ──
  // Single-turn: { "prompt": "...", "system": "...", "max_length": 2048 }
  // Multi-turn:  { "messages": [{"role":"system","content":"..."},
  //                              {"role":"user","content":"..."},
  //                              {"role":"assistant","content":"..."},
  //                              {"role":"user","content":"..."}],
  //                "max_length": 2048 }
  svr.Post("/v1/chat",
           [this](const httplib::Request& req, httplib::Response& res) {
    if (!EnsureLLM(res)) return;

    int max_length = static_cast<int>(
        ExtractJsonNumber(req.body, "max_length", 2048));

    // Try multi-turn first (messages array)
    auto messages = ExtractJsonMessages(req.body);
    if (!messages.empty()) {
      try {
        std::lock_guard<std::mutex> lock(llm_mutex);
        std::string result = llm->Chat(messages, max_length);

        // Auto-save to memory
        std::string session_id = ExtractJsonString(req.body, "session_id");
        if (memory) {
          try {
            if (session_id.empty()) {
              session_id = memory->StartSession("chat");
            }
            // Save the last user message and the assistant response
            for (const auto& m : messages) {
              if (m.role == "system") continue;
              memory->SaveMessage(session_id, m.role, m.content);
            }
            memory->SaveMessage(session_id, "assistant", result);
          } catch (...) {}
        }

        auto body = JsonObj({
            {"text", JsonStr(result)},
            {"session_id", JsonStr(session_id)},
        });
        res.set_content(body, "application/json");
      } catch (const std::exception& e) {
        res.status = 500;
        res.set_content(JsonObj({{"error", JsonStr(e.what())}}),
                        "application/json");
      }
      return;
    }

    // Fall back to single-turn (prompt + system)
    std::string prompt = ExtractJsonString(req.body, "prompt");
    std::string system = ExtractJsonString(req.body, "system");

    if (prompt.empty()) {
      res.status = 400;
      res.set_content(JsonObj({{"error", JsonStr("Missing 'prompt' or 'messages' field")}}),
                      "application/json");
      return;
    }

    if (system.empty()) {
      system = "You are a helpful assistant.";
    }

    try {
      std::lock_guard<std::mutex> lock(llm_mutex);
      std::string result = llm->Chat(system, prompt, max_length);

      // Auto-save to memory
      if (memory) {
        try {
          auto sid = memory->StartSession("chat");
          memory->SaveMessage(sid, "user", prompt);
          memory->SaveMessage(sid, "assistant", result);
          memory->EndSession(sid);
        } catch (...) {}
      }

      auto body = JsonObj({
          {"text", JsonStr(result)},
      });
      res.set_content(body, "application/json");
    } catch (const std::exception& e) {
      res.status = 500;
      res.set_content(JsonObj({{"error", JsonStr(e.what())}}),
                      "application/json");
    }
  });

  // ── POST /v1/chat/stream — Streaming chat (SSE) ──
  svr.Post("/v1/chat/stream",
           [this](const httplib::Request& req, httplib::Response& res) {
    if (!EnsureLLM(res)) return;

    int max_length = static_cast<int>(
        ExtractJsonNumber(req.body, "max_length", 2048));

    auto messages = ExtractJsonMessages(req.body);
    std::string prompt = ExtractJsonString(req.body, "prompt");
    std::string system = ExtractJsonString(req.body, "system");
    std::string session_id = ExtractJsonString(req.body, "session_id");

    if (messages.empty() && prompt.empty()) {
      res.status = 400;
      res.set_content(
          JsonObj({{"error", JsonStr("Missing 'prompt' or 'messages'")}}),
          "application/json");
      return;
    }

    if (system.empty()) system = "You are a helpful assistant.";

    // CORS for streaming
    res.set_header("Access-Control-Allow-Origin", "*");
    res.set_header("Cache-Control", "no-cache");

    // Use chunked transfer with text/event-stream
    res.set_chunked_content_provider(
        "text/event-stream",
        [this, messages, prompt, system, session_id, max_length](
            size_t /*offset*/, httplib::DataSink& sink) -> bool {
          std::string full_result;
          std::string sid = session_id;
          auto on_token = [&](const std::string& token) {
            full_result += token;
            std::string escaped = JsonEscape(token);
            std::string event =
                "data: {\"token\":\"" + escaped + "\"}\n\n";
            sink.write(event.c_str(), event.size());
          };

          try {
            std::lock_guard<std::mutex> lock(llm_mutex);
            if (!messages.empty()) {
              llm->Chat(messages, max_length, on_token);
            } else {
              llm->Chat(system, prompt, max_length, on_token);
            }
          } catch (const std::exception& e) {
            std::string err =
                "data: {\"error\":\"" + JsonEscape(e.what()) + "\"}\n\n";
            sink.write(err.c_str(), err.size());
          }

          // Auto-save to memory
          if (memory && !full_result.empty()) {
            try {
              if (sid.empty()) {
                sid = memory->StartSession("chat");
              }
              if (!messages.empty() && messages.back().role == "user") {
                memory->SaveMessage(sid, "user", messages.back().content);
              } else if (!prompt.empty()) {
                memory->SaveMessage(sid, "user", prompt);
              }
              memory->SaveMessage(sid, "assistant", full_result);
            } catch (...) {}
          }

          // Send done marker with session_id
          std::string done =
              "data: {\"done\":true,\"text\":\"" +
              JsonEscape(full_result) +
              "\",\"session_id\":\"" + JsonEscape(sid) + "\"}\n\n";
          sink.write(done.c_str(), done.size());

          sink.done();
          return true;
        });
  });

  // ── POST /v1/chat/soap ──
  // Body: { "transcript": "..." }
  svr.Post("/v1/chat/soap",
           [this](const httplib::Request& req, httplib::Response& res) {
    if (!EnsureLLM(res)) return;

    std::string transcript = ExtractJsonString(req.body, "transcript");
    if (transcript.empty()) {
      res.status = 400;
      res.set_content(
          JsonObj({{"error", JsonStr("Missing 'transcript' field")}}),
          "application/json");
      return;
    }

    try {
      std::lock_guard<std::mutex> lock(llm_mutex);
      std::string result = llm->GenerateSOAPNotes(transcript);
      res.set_content(JsonObj({{"text", JsonStr(result)}}),
                      "application/json");
    } catch (const std::exception& e) {
      res.status = 500;
      res.set_content(JsonObj({{"error", JsonStr(e.what())}}),
                      "application/json");
    }
  });

  // ── POST /v1/chat/summarize ──
  // Body: { "text": "..." }
  svr.Post("/v1/chat/summarize",
           [this](const httplib::Request& req, httplib::Response& res) {
    if (!EnsureLLM(res)) return;

    std::string text = ExtractJsonString(req.body, "text");
    if (text.empty()) {
      res.status = 400;
      res.set_content(JsonObj({{"error", JsonStr("Missing 'text' field")}}),
                      "application/json");
      return;
    }

    try {
      std::lock_guard<std::mutex> lock(llm_mutex);
      std::string result = llm->Summarize(text);
      res.set_content(JsonObj({{"text", JsonStr(result)}}),
                      "application/json");
    } catch (const std::exception& e) {
      res.status = 500;
      res.set_content(JsonObj({{"error", JsonStr(e.what())}}),
                      "application/json");
    }
  });

  // ── POST /v1/vision/analyze ──
  // Body: multipart form with "image" file + "prompt" field
  // Response: { "text": "..." }
  svr.Post("/v1/vision/analyze",
           [this](const httplib::Request& req, httplib::Response& res) {
    if (!EnsureVision(res)) return;

    if (!req.form.has_file("image")) {
      res.status = 400;
      res.set_content(
          JsonObj({{"error",
                    JsonStr("Missing 'image' file in multipart form")}}),
          "application/json");
      return;
    }

    auto file = req.form.get_file("image");
    std::string prompt = "Describe this image in detail.";
    if (req.form.has_field("prompt")) {
      prompt = req.form.get_field("prompt");
    }

    // Write image to temp file
    auto temp_path = fs::temp_directory_path() / "EDGESCRIBE_image.tmp";
    {
      std::ofstream ofs(temp_path, std::ios::binary);
      ofs.write(file.content.data(),
                static_cast<std::streamsize>(file.content.size()));
    }

    try {
      std::lock_guard<std::mutex> lock(vision_mutex);
      std::string result = vision->Analyze(temp_path.string(), prompt);
      fs::remove(temp_path);

      res.set_content(JsonObj({{"text", JsonStr(result)}}),
                      "application/json");
    } catch (const std::exception& e) {
      fs::remove(temp_path);
      res.status = 500;
      res.set_content(JsonObj({{"error", JsonStr(e.what())}}),
                      "application/json");
    }
  });

  // ── POST /v1/vision/ocr ──
  // Body: multipart form with "image" file
  svr.Post("/v1/vision/ocr",
           [this](const httplib::Request& req, httplib::Response& res) {
    if (!EnsureVision(res)) return;

    if (!req.form.has_file("image")) {
      res.status = 400;
      res.set_content(
          JsonObj({{"error",
                    JsonStr("Missing 'image' file in multipart form")}}),
          "application/json");
      return;
    }

    auto file = req.form.get_file("image");
    auto temp_path = fs::temp_directory_path() / "EDGESCRIBE_ocr.tmp";
    {
      std::ofstream ofs(temp_path, std::ios::binary);
      ofs.write(file.content.data(),
                static_cast<std::streamsize>(file.content.size()));
    }

    try {
      std::lock_guard<std::mutex> lock(vision_mutex);
      std::string result = vision->OCR(temp_path.string());
      fs::remove(temp_path);

      res.set_content(JsonObj({{"text", JsonStr(result)}}),
                      "application/json");
    } catch (const std::exception& e) {
      fs::remove(temp_path);
      res.status = 500;
      res.set_content(JsonObj({{"error", JsonStr(e.what())}}),
                      "application/json");
    }
  });

  // ── POST /v1/tts/synthesize ──
  // Body: { "text": "...", "voice": "af_heart", "speed": 1.0 }
  // Response: WAV audio file
  svr.Post("/v1/tts/synthesize",
           [this](const httplib::Request& req, httplib::Response& res) {
    if (!EnsureTTS(res)) return;

    std::string text = ExtractJsonString(req.body, "text");
    std::string voice = ExtractJsonString(req.body, "voice");
    float speed = static_cast<float>(
        ExtractJsonNumber(req.body, "speed", 1.0));

    if (text.empty()) {
      res.status = 400;
      res.set_content(JsonObj({{"error", JsonStr("Missing 'text' field")}}),
                      "application/json");
      return;
    }

    if (voice.empty()) voice = "af_heart";

    try {
      std::lock_guard<std::mutex> lock(tts_mutex);
      auto audio = tts->Synthesize(text, voice, speed);

      // Write to temp WAV and return
      auto temp_path = fs::temp_directory_path() / "EDGESCRIBE_tts.wav";
      tts::TtsEngine::WriteWav(temp_path.string(), audio.samples.data(),
                               audio.samples.size(), audio.sample_rate);

      std::ifstream ifs(temp_path, std::ios::binary);
      std::string wav_data((std::istreambuf_iterator<char>(ifs)),
                            std::istreambuf_iterator<char>());
      fs::remove(temp_path);

      res.set_content(wav_data, "audio/wav");
    } catch (const std::exception& e) {
      res.status = 500;
      res.set_content(JsonObj({{"error", JsonStr(e.what())}}),
                      "application/json");
    }
  });

  // ── GET /v1/tts/voices ──
  svr.Get("/v1/tts/voices",
          [this](const httplib::Request&, httplib::Response& res) {
    if (!EnsureTTS(res)) return;

    auto voices = tts->ListVoices();
    std::ostringstream ss;
    ss << "{\"voices\":[";
    for (size_t i = 0; i < voices.size(); i++) {
      if (i > 0) ss << ",";
      ss << JsonStr(voices[i]);
    }
    ss << "]}";
    res.set_content(ss.str(), "application/json");
  });

  // ── Memory / History Endpoints ──

  // GET /v1/memory/sessions — list recent sessions
  svr.Get("/v1/memory/sessions",
          [this](const httplib::Request&, httplib::Response& res) {
    if (!memory) {
      res.status = 503;
      res.set_content(JsonObj({{"error", JsonStr("Memory store not available")}}),
                      "application/json");
      return;
    }

    auto sessions = memory->GetRecentSessions(50);
    std::ostringstream ss;
    ss << "{\"sessions\":[";
    for (size_t i = 0; i < sessions.size(); i++) {
      if (i > 0) ss << ",";
      ss << JsonObj({
          {"id", JsonStr(sessions[i].id)},
          {"type", JsonStr(sessions[i].type)},
          {"model", JsonStr(sessions[i].model)},
          {"started_at", JsonStr(sessions[i].started_at)},
          {"message_count", std::to_string(sessions[i].message_count)},
      });
    }
    ss << "]}";
    res.set_content(ss.str(), "application/json");
  });

  // GET /v1/memory/sessions/:id — get messages for a session
  svr.Get(R"(/v1/memory/sessions/([a-zA-Z0-9_]+))",
          [this](const httplib::Request& req, httplib::Response& res) {
    if (!memory) {
      res.status = 503;
      res.set_content(JsonObj({{"error", JsonStr("Memory store not available")}}),
                      "application/json");
      return;
    }

    std::string session_id = req.matches[1];
    auto messages = memory->GetMessages(session_id);
    auto notes = memory->GetNotes(session_id);

    std::ostringstream ss;
    ss << "{\"session_id\":" << JsonStr(session_id);
    ss << ",\"messages\":[";
    for (size_t i = 0; i < messages.size(); i++) {
      if (i > 0) ss << ",";
      ss << JsonObj({
          {"role", JsonStr(messages[i].role)},
          {"content", JsonStr(messages[i].content)},
          {"created_at", JsonStr(messages[i].created_at)},
      });
    }
    ss << "],\"notes\":[";
    for (size_t i = 0; i < notes.size(); i++) {
      if (i > 0) ss << ",";
      ss << JsonObj({
          {"type", JsonStr(notes[i].type)},
          {"output_text", JsonStr(notes[i].output_text)},
          {"created_at", JsonStr(notes[i].created_at)},
      });
    }
    ss << "]}";
    res.set_content(ss.str(), "application/json");
  });

  // POST /v1/memory/search — search messages
  svr.Post("/v1/memory/search",
           [this](const httplib::Request& req, httplib::Response& res) {
    if (!memory) {
      res.status = 503;
      res.set_content(JsonObj({{"error", JsonStr("Memory store not available")}}),
                      "application/json");
      return;
    }

    std::string query = ExtractJsonString(req.body, "query");
    if (query.empty()) {
      res.status = 400;
      res.set_content(JsonObj({{"error", JsonStr("Missing 'query' field")}}),
                      "application/json");
      return;
    }

    auto results = memory->SearchMessages(query, 20);
    std::ostringstream ss;
    ss << "{\"results\":[";
    for (size_t i = 0; i < results.size(); i++) {
      if (i > 0) ss << ",";
      ss << JsonObj({
          {"session_id", JsonStr(results[i].session_id)},
          {"role", JsonStr(results[i].role)},
          {"content", JsonStr(results[i].content)},
          {"created_at", JsonStr(results[i].created_at)},
      });
    }
    ss << "]}";
    res.set_content(ss.str(), "application/json");
  });

  // DELETE /v1/memory/sessions/:id — delete a session
  svr.Delete(R"(/v1/memory/sessions/([a-zA-Z0-9_]+))",
             [this](const httplib::Request& req, httplib::Response& res) {
    if (!memory) {
      res.status = 503;
      res.set_content(JsonObj({{"error", JsonStr("Memory store not available")}}),
                      "application/json");
      return;
    }

    std::string session_id = req.matches[1];
    memory->DeleteSession(session_id);
    res.set_content(JsonObj({{"deleted", JsonStr(session_id)}}),
                    "application/json");
  });
}

// ── Public API ──────────────────────────────────────────────────────────────
ApiServer::ApiServer(const ServerConfig& config)
    : impl_(std::make_unique<Impl>()) {
  impl_->config = config;
}

ApiServer::~ApiServer() {
  Stop();
}

void ApiServer::Start() {
  std::cout << std::string(60, '=') << std::endl;
  std::cout << "  EDGESCRIBE API Server" << std::endl;
  std::cout << "  http://" << impl_->config.host << ":"
            << impl_->config.port << std::endl;
  std::cout << std::string(60, '=') << std::endl;
  std::cout << std::endl;

  std::cout << "Initializing..." << std::endl;
  impl_->running = true;
  impl_->InitEngines();

  std::cout << std::endl;
  std::cout << "Registering routes..." << std::endl;
  impl_->RegisterRoutes();

  std::cout << std::endl;
  std::cout << "Endpoints:" << std::endl;
  std::cout << "  GET  /v1/health              Health check" << std::endl;
  std::cout << "  GET  /v1/models              List loaded models" << std::endl;
  if (impl_->transcriber) {
    std::cout << "  POST /v1/transcribe/file     Transcribe WAV file"
              << std::endl;
    std::cout << "  POST /v1/transcribe/stream   Start streaming session"
              << std::endl;
    std::cout << "  POST /v1/transcribe/push     Push audio chunk"
              << std::endl;
    std::cout << "  POST /v1/transcribe/flush    Flush and get final text"
              << std::endl;
  }
  if (impl_->llm) {
    std::cout << "  POST /v1/chat                Chat completion" << std::endl;
    std::cout << "  POST /v1/chat/soap           Generate SOAP notes"
              << std::endl;
    std::cout << "  POST /v1/chat/summarize      Summarize text" << std::endl;
  }
  if (impl_->vision) {
    std::cout << "  POST /v1/vision/analyze      Analyze image" << std::endl;
    std::cout << "  POST /v1/vision/ocr          OCR image" << std::endl;
  }
  if (impl_->tts) {
    std::cout << "  POST /v1/tts/synthesize      Text to speech" << std::endl;
    std::cout << "  GET  /v1/tts/voices          List voices" << std::endl;
  }

  std::cout << std::endl;
  std::cout << "Server ready. Press Ctrl+C to stop." << std::endl;

  // ── Serve static frontend files ──
  // Try multiple locations: ./www, ../www, exe_dir/www
  std::vector<std::string> www_search = {"www", "../www"};
  bool www_found = false;
  for (const auto& dir : www_search) {
    if (fs::exists(dir) && fs::exists(fs::path(dir) / "index.html")) {
      impl_->svr.set_mount_point("/", dir);
      std::cout << "  UI: http://" << impl_->config.host << ":"
                << impl_->config.port << "/" << std::endl;
      www_found = true;
      break;
    }
  }
  if (!www_found) {
    std::cout << "  UI: not found (place www/index.html next to the binary)" << std::endl;
  }

  std::cout << std::endl;

  impl_->running = true;
  impl_->svr.listen(impl_->config.host, impl_->config.port);
}

void ApiServer::Stop() {
  if (impl_->running) {
    impl_->running = false;
    impl_->svr.stop();
    impl_->StopIdleMonitor();
  }
}

bool ApiServer::IsRunning() const {
  return impl_->running;
}

}  // namespace EDGESCRIBE::server
