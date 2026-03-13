// EDGESCRIBE — Vision/OCR Engine Implementation
// Uses onnxruntime-genai MultiModalProcessor for image + text understanding

#include "vision/vision_engine.h"
#include "ort_genai.h"
#include <iostream>
#include <sstream>

namespace EDGESCRIBE::vision {

static std::unique_ptr<OgaModel> CreateModelWithDevice(
    const std::string& model_path, const std::string& device) {
  if (device.empty() || device == "cpu") {
    return OgaModel::Create(model_path.c_str());
  }
  auto config = OgaConfig::Create(model_path.c_str());
  config->ClearProviders();
  config->AppendProvider(device.c_str());
  config->AppendProvider("cpu");
  return OgaModel::Create(*config);
}

VisionEngine::VisionEngine(const std::string& model_path,
                           const std::string& device) {
  model_ = CreateModelWithDevice(model_path, device);
  tokenizer_ = OgaTokenizer::Create(*model_);
  processor_ = OgaMultiModalProcessor::Create(*model_);

  // Detect model type for proper prompt formatting
  OgaString type = model_->GetType();
  model_type_ = std::string(static_cast<const char*>(type));
}

VisionEngine::~VisionEngine() = default;

std::string VisionEngine::Analyze(const std::string& image_path,
                                  const std::string& prompt,
                                  int max_length,
                                  TokenCallback on_token) {
  return RunVisionGeneration(prompt, {image_path}, max_length, on_token);
}

std::string VisionEngine::Analyze(const std::vector<std::string>& image_paths,
                                  const std::string& prompt,
                                  int max_length,
                                  TokenCallback on_token) {
  return RunVisionGeneration(prompt, image_paths, max_length, on_token);
}

std::string VisionEngine::OCR(const std::string& image_path,
                              TokenCallback on_token) {
  return Analyze(
      image_path,
      "Extract all text from this image. Return the text exactly as written, "
      "preserving layout and formatting where possible.",
      4096,
      on_token);
}

std::string VisionEngine::GenerateSOAPNotes(const std::string& transcript,
                                            const std::string& image_path,
                                            TokenCallback on_token) {
  std::string prompt =
      "You are a medical scribe. Based on the following information, generate "
      "structured SOAP notes.\n\nTranscript:\n" + transcript;

  if (!image_path.empty()) {
    prompt += "\n\nAlso consider the attached medical image in your assessment.";
    return Analyze(image_path, prompt, 4096, on_token);
  }

  // Text-only: use as vision model with no image
  return RunVisionGeneration(prompt, {}, 4096, on_token);
}

std::string VisionEngine::DescribeMedicalImage(const std::string& image_path,
                                               TokenCallback on_token) {
  return Analyze(
      image_path,
      "Describe this medical image in detail. Include any relevant findings, "
      "abnormalities, or notable features. Use proper medical terminology.",
      2048,
      on_token);
}

std::string VisionEngine::FormatVisionPrompt(const std::string& prompt,
                                             int num_images) {
  // Build the content string with image placeholders based on model type
  std::string content;

  if (model_type_.find("qwen") != std::string::npos ||
      model_type_.find("Qwen") != std::string::npos) {
    // Qwen-VL format
    for (int i = 0; i < num_images; i++) {
      content += "<|vision_start|><|image_pad|><|vision_end|>";
    }
    content += prompt;
  } else if (model_type_.find("phi3v") != std::string::npos) {
    // Phi-3 Vision format
    for (int i = 0; i < num_images; i++) {
      content += "<|image_" + std::to_string(i + 1) + "|>\n";
    }
    content += prompt;
  } else if (model_type_.find("phi4") != std::string::npos) {
    // Phi-4 multimodal format
    for (int i = 0; i < num_images; i++) {
      content += "<|image_" + std::to_string(i + 1) + "|>\n";
    }
    content += prompt;
  } else {
    // Generic: try image tags
    for (int i = 0; i < num_images; i++) {
      content += "<|image_" + std::to_string(i + 1) + "|>\n";
    }
    content += prompt;
  }

  // Build chat messages JSON
  std::ostringstream json;
  json << "[{\"role\":\"user\",\"content\":\"";
  for (char c : content) {
    switch (c) {
      case '"': json << "\\\""; break;
      case '\\': json << "\\\\"; break;
      case '\n': json << "\\n"; break;
      case '\r': json << "\\r"; break;
      case '\t': json << "\\t"; break;
      default: json << c; break;
    }
  }
  json << "\"}]";

  // Apply chat template
  OgaString result = tokenizer_->ApplyChatTemplate(
      nullptr, json.str().c_str(), nullptr, true);

  return std::string(static_cast<const char*>(result));
}

std::string VisionEngine::RunVisionGeneration(
    const std::string& prompt,
    const std::vector<std::string>& image_paths,
    int max_length,
    TokenCallback on_token) {

  // Format prompt with image placeholders
  std::string formatted = FormatVisionPrompt(prompt,
                                              static_cast<int>(image_paths.size()));

  // Load images if provided
  std::unique_ptr<OgaImages> images;
  if (!image_paths.empty()) {
    std::vector<const char*> paths;
    for (const auto& p : image_paths) {
      paths.push_back(p.c_str());
    }
    images = OgaImages::Load(paths);
  }

  // Process images + prompt through multi-modal processor
  auto input_tensors = processor_->ProcessImages(
      formatted.c_str(),
      images.get());

  // Create generator
  auto params = OgaGeneratorParams::Create(*model_);
  params->SetSearchOption("max_length", static_cast<double>(max_length));

  auto generator = OgaGenerator::Create(*model_, *params);
  auto token_stream = OgaTokenizerStream::Create(*tokenizer_);

  // Set processed inputs
  generator->SetInputs(*input_tensors);

  // Generate tokens
  std::string result;
  while (!generator->IsDone()) {
    generator->GenerateNextToken();
    auto tokens = generator->GetNextTokens();
    if (!tokens.empty()) {
      const char* text = token_stream->Decode(tokens[0]);
      if (text && text[0] != '\0') {
        result += text;
        if (on_token) {
          on_token(std::string(text));
        }
      }
    }
  }

  return result;
}

}  // namespace EDGESCRIBE::vision
