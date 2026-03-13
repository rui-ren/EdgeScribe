// EDGESCRIBE — Model Manager
// Handles model download from HuggingFace and local cache management

#pragma once

#include <functional>
#include <string>
#include <vector>

namespace EDGESCRIBE {

struct ModelInfo {
  std::string name;         // e.g., "nemotron"
  std::string display_name; // e.g., "Parakeet TDT 0.6B"
  std::string repo;         // e.g., "EDGESCRIBE/nemotron-onnx"
  std::string description;
  std::string type;         // "asr", "vlm", "tts"
  std::string config_file;  // Config file to check if cached (e.g., "genai_config.json")
  size_t size_mb;
  bool cached;              // Whether the model is downloaded
  std::string local_path;   // Path on disk (if cached)
};

// Progress callback: (bytes_downloaded, total_bytes)
using ProgressCallback = std::function<void(size_t downloaded, size_t total)>;

class ModelManager {
 public:
  ModelManager();
  ~ModelManager();

  // Download a model from HuggingFace
  void Pull(const std::string& model_name, ProgressCallback progress = nullptr);

  // Get the local path to a cached model
  std::string GetModelPath(const std::string& model_name) const;

  // Check if a model is cached locally
  bool IsCached(const std::string& model_name) const;

  // List all available models (from built-in manifest)
  std::vector<ModelInfo> ListAvailable() const;

  // List downloaded models
  std::vector<ModelInfo> ListCached() const;

  // Remove a cached model
  void Remove(const std::string& model_name);

  // Get the cache directory
  const std::string& GetCacheDir() const { return cache_dir_; }

 private:
  std::string cache_dir_;

  // Initialize default cache directory
  void InitCacheDir();

  // Get model info from manifest
  ModelInfo GetModelInfo(const std::string& model_name) const;

  // Download a single file from HuggingFace
  void DownloadFile(const std::string& url, const std::string& dest_path,
                    ProgressCallback progress = nullptr);
};

}  // namespace EDGESCRIBE
