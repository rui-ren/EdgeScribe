// EDGESCRIBE — Vision/OCR Engine
// Wraps onnxruntime-genai MultiModalProcessor for image understanding (Qwen3-VL)

#pragma once

#include <functional>
#include <memory>
#include <string>
#include <vector>

// Forward declarations
struct OgaModel;
struct OgaTokenizer;
struct OgaTokenizerStream;
struct OgaMultiModalProcessor;

namespace EDGESCRIBE::vision {

// Callback for streaming token output
using TokenCallback = std::function<void(const std::string& token)>;

class VisionEngine {
 public:
  explicit VisionEngine(const std::string& model_path,
                        const std::string& device = "cpu");
  ~VisionEngine();

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

  std::string FormatVisionPrompt(const std::string& prompt,
                                 int num_images);

  std::unique_ptr<OgaModel> model_;
  std::unique_ptr<OgaTokenizer> tokenizer_;
  std::unique_ptr<OgaMultiModalProcessor> processor_;
  std::string model_type_;
};

}  // namespace EDGESCRIBE::vision
