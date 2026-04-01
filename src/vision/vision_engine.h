// EDGESCRIBE — Vision/OCR Engine
// Wraps llama.cpp for image understanding (Qwen3-VL via GGUF)

#pragma once

#include <functional>
#include <memory>
#include <string>
#include <vector>

struct llama_model;
struct llama_context;
struct llama_sampler;
struct mtmd_context;

namespace EDGESCRIBE::vision {

// Callback for streaming token output
using TokenCallback = std::function<void(const std::string& token)>;

class VisionEngine {
 public:
  explicit VisionEngine(const std::string& model_path,
                        const std::string& device = "cpu");
  ~VisionEngine();

  VisionEngine(const VisionEngine&) = delete;
  VisionEngine& operator=(const VisionEngine&) = delete;

  // Analyze an image with a text prompt
  std::string Analyze(const std::string& image_path,
                      const std::string& prompt,
                      int max_length = 2048,
                      TokenCallback on_token = nullptr);

  // Analyze multiple images
  std::string Analyze(const std::vector<std::string>& image_paths,
                      const std::string& prompt,
                      int max_length = 2048,
                      TokenCallback on_token = nullptr);

  // OCR: Extract text from an image
  std::string OCR(const std::string& image_path,
                  TokenCallback on_token = nullptr);

  // Generate SOAP notes from transcript + optional image
  std::string GenerateSOAPNotes(const std::string& transcript,
                                const std::string& image_path = "",
                                TokenCallback on_token = nullptr);

  // Describe a medical image
  std::string DescribeMedicalImage(const std::string& image_path,
                                   TokenCallback on_token = nullptr);

 private:
  std::string RunVisionGeneration(const std::string& prompt,
                                  const std::vector<std::string>& image_paths,
                                  int max_length,
                                  TokenCallback on_token);

  void ResetContext();

  llama_model* model_ = nullptr;
  llama_context* ctx_ = nullptr;
  llama_sampler* sampler_ = nullptr;
  mtmd_context* mtmd_ctx_ = nullptr;
  std::string model_path_;
  int n_ctx_ = 4096;
};

}  // namespace EDGESCRIBE::vision
