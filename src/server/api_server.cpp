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

// ── Server Implementation ───────────────────────────────────────────────────
struct ApiServer::Impl {
  httplib::Server svr;
  ServerConfig config;

  // Engines (lazy-initialized)
  std::unique_ptr<Transcriber> transcriber;
  std::unique_ptr<llm::LlmEngine> llm;
  std::unique_ptr<vision::VisionEngine> vision;
  std::unique_ptr<tts::TtsEngine> tts;

  std::mutex asr_mutex;
  std::mutex llm_mutex;
  std::mutex vision_mutex;
  std::mutex tts_mutex;

  bool running = false;

  void InitEngines();
  void RegisterRoutes();
};

void ApiServer::Impl::InitEngines() {
  if (!config.asr_model.empty() && fs::exists(config.asr_model)) {
    std::cout << "  Loading ASR model..." << std::endl;
    transcriber = std::make_unique<Transcriber>(config.asr_model, config.device);
  }

  if (!config.vlm_model.empty() && fs::exists(config.vlm_model)) {
    std::cout << "  Loading VLM/LLM model..." << std::endl;
    llm = std::make_unique<llm::LlmEngine>(config.vlm_model, config.device);
    vision = std::make_unique<vision::VisionEngine>(config.vlm_model, config.device);
  }

  if (!config.tts_model.empty() && fs::exists(config.tts_model)) {
    std::cout << "  Loading TTS model..." << std::endl;
    tts = std::make_unique<tts::TtsEngine>(config.tts_model);
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
    auto body = JsonObj({
        {"status", JsonStr("ok")},
        {"version", JsonStr("0.1.0")},
        {"engines", JsonObj({
            {"asr", JsonBool(transcriber != nullptr)},
            {"llm", JsonBool(llm != nullptr)},
            {"vision", JsonBool(vision != nullptr)},
            {"tts", JsonBool(tts != nullptr)},
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
    if (!transcriber) {
      res.status = 503;
      res.set_content(JsonObj({{"error", JsonStr("ASR model not loaded")}}),
                      "application/json");
      return;
    }

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
    if (!transcriber) {
      res.status = 503;
      res.set_content(JsonObj({{"error", JsonStr("ASR model not loaded")}}),
                      "application/json");
      return;
    }

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
    if (!transcriber) {
      res.status = 503;
      res.set_content(JsonObj({{"error", JsonStr("ASR model not loaded")}}),
                      "application/json");
      return;
    }

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
    if (!transcriber) {
      res.status = 503;
      res.set_content(JsonObj({{"error", JsonStr("ASR model not loaded")}}),
                      "application/json");
      return;
    }

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
  svr.Post("/v1/chat",
           [this](const httplib::Request& req, httplib::Response& res) {
    if (!llm) {
      res.status = 503;
      res.set_content(JsonObj({{"error", JsonStr("LLM model not loaded")}}),
                      "application/json");
      return;
    }

    std::string prompt = ExtractJsonString(req.body, "prompt");
    std::string system = ExtractJsonString(req.body, "system");
    int max_length = static_cast<int>(
        ExtractJsonNumber(req.body, "max_length", 2048));

    if (prompt.empty()) {
      res.status = 400;
      res.set_content(JsonObj({{"error", JsonStr("Missing 'prompt' field")}}),
                      "application/json");
      return;
    }

    if (system.empty()) {
      system = "You are a helpful assistant.";
    }

    try {
      std::lock_guard<std::mutex> lock(llm_mutex);
      std::string result = llm->Chat(system, prompt, max_length);

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

  // ── POST /v1/chat/soap ──
  // Body: { "transcript": "..." }
  svr.Post("/v1/chat/soap",
           [this](const httplib::Request& req, httplib::Response& res) {
    if (!llm) {
      res.status = 503;
      res.set_content(JsonObj({{"error", JsonStr("LLM model not loaded")}}),
                      "application/json");
      return;
    }

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
    if (!llm) {
      res.status = 503;
      res.set_content(JsonObj({{"error", JsonStr("LLM model not loaded")}}),
                      "application/json");
      return;
    }

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
    if (!vision) {
      res.status = 503;
      res.set_content(
          JsonObj({{"error", JsonStr("Vision model not loaded")}}),
          "application/json");
      return;
    }

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
    if (!vision) {
      res.status = 503;
      res.set_content(
          JsonObj({{"error", JsonStr("Vision model not loaded")}}),
          "application/json");
      return;
    }

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
    if (!tts) {
      res.status = 503;
      res.set_content(JsonObj({{"error", JsonStr("TTS model not loaded")}}),
                      "application/json");
      return;
    }

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
    if (!tts) {
      res.status = 503;
      res.set_content(JsonObj({{"error", JsonStr("TTS model not loaded")}}),
                      "application/json");
      return;
    }

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
  std::cout << "════════════════════════════════════════════════════════════"
            << std::endl;
  std::cout << "  EDGESCRIBE API Server" << std::endl;
  std::cout << "  http://" << impl_->config.host << ":"
            << impl_->config.port << std::endl;
  std::cout << "════════════════════════════════════════════════════════════"
            << std::endl;
  std::cout << std::endl;

  std::cout << "Loading models..." << std::endl;
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
    impl_->svr.stop();
    impl_->running = false;
  }
}

bool ApiServer::IsRunning() const {
  return impl_->running;
}

}  // namespace EDGESCRIBE::server
