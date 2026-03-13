// EDGESCRIBE — Model Manager Implementation
// Downloads models from HuggingFace and manages local cache

#include "core/model_manager.h"

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>

#ifdef _WIN32
#include <shlobj.h>
#pragma comment(lib, "shell32.lib")
#else
#include <pwd.h>
#include <unistd.h>
#endif

namespace fs = std::filesystem;

namespace EDGESCRIBE {

// Built-in model manifest
static const struct {
  const char* name;
  const char* display_name;
  const char* repo;
  const char* description;
  const char* type;         // "asr", "vlm", "tts"
  const char* config_file;  // File to check if model is cached
  size_t size_mb;
  // Files to download from the HuggingFace repo
  const char* files[10];
  size_t file_count;
} kModelManifest[] = {
    {
        "nemotron",
        "Parakeet TDT 0.6B",
        "EDGESCRIBE/nemotron-onnx-cpu",
        "Real-time English speech-to-text. CPU optimized.",
        "asr",
        "genai_config.json",
        670,
        {"encoder.onnx", "decoder.onnx", "joint.onnx", "genai_config.json",
         "audio_processor_config.json", "tokenizer.json"},
        6,
    },
    {
        "qwen3-vl",
        "Qwen3-VL-2B INT4",
        "EDGESCRIBE/qwen3-vl-2b-onnx-cpu-int4",
        "Vision + language. OCR, image understanding, SOAP notes. CPU INT4.",
        "vlm",
        "genai_config.json",
        1500,
        {"model.onnx", "model.onnx.data", "genai_config.json",
         "tokenizer.json", "tokenizer_config.json", "special_tokens_map.json",
         "preprocessor_config.json"},
        7,
    },
    {
        "kokoro",
        "Kokoro TTS",
        "EDGESCRIBE/kokoro-onnx",
        "Text-to-speech. Natural voices. CPU optimized.",
        "tts",
        "model.onnx",
        300,
        {"model.onnx", "voices.bin", "config.json"},
        3,
    },
    {
        "speakers",
        "ECAPA-TDNN Speaker",
        "EDGESCRIBE/ecapa-tdnn-onnx",
        "Speaker diarization. Identifies who is speaking. CPU optimized.",
        "diarizer",
        "speaker_model.onnx",
        15,
        {"speaker_model.onnx", "config.json"},
        2,
    },
};

static constexpr size_t kModelCount = sizeof(kModelManifest) / sizeof(kModelManifest[0]);

ModelManager::ModelManager() {
  InitCacheDir();
}

ModelManager::~ModelManager() = default;

void ModelManager::InitCacheDir() {
#ifdef _WIN32
  // Use %LOCALAPPDATA%/EDGESCRIBE/models or %USERPROFILE%/.EDGESCRIBE/models
  const char* local_app_data = std::getenv("LOCALAPPDATA");
  if (local_app_data) {
    cache_dir_ = std::string(local_app_data) + "\\EDGESCRIBE\\models";
  } else {
    const char* home = std::getenv("USERPROFILE");
    if (home) {
      cache_dir_ = std::string(home) + "\\.EDGESCRIBE\\models";
    } else {
      cache_dir_ = ".EDGESCRIBE\\models";
    }
  }
#else
  // Use ~/.EDGESCRIBE/models
  const char* home = std::getenv("HOME");
  if (!home) {
    struct passwd* pw = getpwuid(getuid());
    home = pw ? pw->pw_dir : ".";
  }
  cache_dir_ = std::string(home) + "/.EDGESCRIBE/models";
#endif

  // Allow override via environment variable
  const char* override_dir = std::getenv("EDGESCRIBE_MODEL_DIR");
  if (override_dir && override_dir[0] != '\0') {
    cache_dir_ = override_dir;
  }
}

ModelInfo ModelManager::GetModelInfo(const std::string& model_name) const {
  for (size_t i = 0; i < kModelCount; i++) {
    if (model_name == kModelManifest[i].name) {
      ModelInfo info;
      info.name = kModelManifest[i].name;
      info.display_name = kModelManifest[i].display_name;
      info.repo = kModelManifest[i].repo;
      info.description = kModelManifest[i].description;
      info.type = kModelManifest[i].type;
      info.config_file = kModelManifest[i].config_file;
      info.size_mb = kModelManifest[i].size_mb;
      info.local_path = cache_dir_ + "/" + model_name;
      info.cached = IsCached(model_name);
      return info;
    }
  }
  throw std::runtime_error("Unknown model: " + model_name +
                           "\nRun 'EDGESCRIBE list' to see available models.");
}

std::string ModelManager::GetModelPath(const std::string& model_name) const {
  // First check if model_name is actually a direct path
  if (fs::exists(model_name) && fs::is_directory(model_name)) {
    // Accept if it contains any known config file
    if (fs::exists(fs::path(model_name) / "genai_config.json") ||
        fs::exists(fs::path(model_name) / "config.json") ||
        fs::exists(fs::path(model_name) / "model.onnx")) {
      return model_name;
    }
  }

  std::string path = cache_dir_ + "/" + model_name;
  if (!fs::exists(path)) {
    throw std::runtime_error("Model '" + model_name + "' is not downloaded.\n"
                             "Run: EDGESCRIBE pull " + model_name);
  }
  return path;
}

bool ModelManager::IsCached(const std::string& model_name) const {
  fs::path model_dir = fs::path(cache_dir_) / model_name;

  // Check using the model's specific config file from manifest
  for (size_t i = 0; i < kModelCount; i++) {
    if (model_name == kModelManifest[i].name) {
      return fs::exists(model_dir / kModelManifest[i].config_file);
    }
  }

  // Fallback: check for any known config file
  return fs::exists(model_dir / "genai_config.json") ||
         fs::exists(model_dir / "config.json") ||
         fs::exists(model_dir / "model.onnx");
}

std::vector<ModelInfo> ModelManager::ListAvailable() const {
  std::vector<ModelInfo> models;
  for (size_t i = 0; i < kModelCount; i++) {
    ModelInfo info;
    info.name = kModelManifest[i].name;
    info.display_name = kModelManifest[i].display_name;
    info.repo = kModelManifest[i].repo;
    info.description = kModelManifest[i].description;
    info.type = kModelManifest[i].type;
    info.config_file = kModelManifest[i].config_file;
    info.size_mb = kModelManifest[i].size_mb;
    info.local_path = cache_dir_ + "/" + info.name;
    info.cached = IsCached(info.name);
    models.push_back(info);
  }
  return models;
}

std::vector<ModelInfo> ModelManager::ListCached() const {
  std::vector<ModelInfo> cached;
  auto all = ListAvailable();
  std::copy_if(all.begin(), all.end(), std::back_inserter(cached),
               [](const ModelInfo& m) { return m.cached; });
  return cached;
}

void ModelManager::Remove(const std::string& model_name) {
  fs::path model_dir = fs::path(cache_dir_) / model_name;
  if (fs::exists(model_dir)) {
    fs::remove_all(model_dir);
    std::cout << "Removed model: " << model_name << std::endl;
  } else {
    std::cout << "Model '" << model_name << "' is not downloaded." << std::endl;
  }
}

void ModelManager::Pull(const std::string& model_name, ProgressCallback progress) {
  auto info = GetModelInfo(model_name);

  if (IsCached(model_name)) {
    std::cout << "Model '" << model_name << "' is already downloaded." << std::endl;
    std::cout << "Path: " << GetModelPath(model_name) << std::endl;
    return;
  }

  fs::path model_dir = fs::path(cache_dir_) / model_name;
  fs::create_directories(model_dir);

  std::cout << "Downloading " << info.display_name << " (~" << info.size_mb << " MB)..." << std::endl;
  std::cout << "From: huggingface.co/" << info.repo << std::endl;
  std::cout << "To:   " << model_dir.string() << std::endl;
  std::cout << std::endl;

  // Find the manifest entry
  const auto* manifest = &kModelManifest[0];
  for (size_t i = 0; i < kModelCount; i++) {
    if (model_name == kModelManifest[i].name) {
      manifest = &kModelManifest[i];
      break;
    }
  }

  for (size_t i = 0; i < manifest->file_count; i++) {
    std::string filename = manifest->files[i];
    std::string url = "https://huggingface.co/" + info.repo +
                      "/resolve/main/" + filename;
    std::string dest = (model_dir / filename).string();

    std::cout << "  " << filename << " ... " << std::flush;
    DownloadFile(url, dest, progress);
    std::cout << "done" << std::endl;
  }

  std::cout << std::endl;
  std::cout << "Model downloaded successfully!" << std::endl;
  std::cout << "Run: EDGESCRIBE run --live --model " << model_name << std::endl;
}

void ModelManager::DownloadFile(const std::string& url,
                                const std::string& dest_path,
                                ProgressCallback progress) {
  // Validate URL — only allow HuggingFace downloads
  if (url.find("https://huggingface.co/") != 0) {
    throw std::runtime_error("Invalid download URL: " + url +
                             "\nOnly HuggingFace URLs are supported.");
  }

  // Validate no shell metacharacters in URL and path
  auto has_shell_chars = [](const std::string& s) {
    return s.find_first_of(";|&`$(){}[]!#") != std::string::npos;
  };
  if (has_shell_chars(url) || has_shell_chars(dest_path)) {
    throw std::runtime_error("Invalid characters in URL or path.");
  }

  // Use platform-native tools for HTTP download
  std::string command;

#ifdef _WIN32
  command = "powershell -NoProfile -Command \"& { "
            "$ProgressPreference = 'SilentlyContinue'; "
            "Invoke-WebRequest -Uri '" + url + "' "
            "-OutFile '" + dest_path + "' "
            "-UseBasicParsing }\"";
#else
  command = "curl -sL -o '" + dest_path + "' '" + url + "'";
#endif

  int result = std::system(command.c_str());
  if (result != 0) {
    fs::remove(dest_path);
    throw std::runtime_error("Failed to download: " + url +
                             "\nMake sure you have internet access.");
  }

  if (!fs::exists(dest_path) || fs::file_size(dest_path) == 0) {
    fs::remove(dest_path);
    throw std::runtime_error("Downloaded file is empty or missing: " + dest_path);
  }
}

}  // namespace EDGESCRIBE
