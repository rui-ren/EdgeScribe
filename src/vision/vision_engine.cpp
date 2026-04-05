// EDGESCRIBE — Vision/OCR Engine Implementation
// Uses llama.cpp with mtmd (multimodal) for image + text understanding (Qwen3-VL GGUF)

#include "vision/vision_engine.h"

#include <llama.h>
#include <ggml-backend.h>
#include <mtmd.h>
#include <mtmd-helper.h>

#include <iostream>
#include <filesystem>
#include <sstream>
#include <stdexcept>
#include <vector>

namespace EDGESCRIBE::vision {

static int DetectGpuLayers(const std::string& device) {
  if (device == "cpu") return 0;
  return 999;
}

VisionEngine::VisionEngine(const std::string& model_path,
                           const std::string& device)
    : model_path_(model_path) {
  // Suppress verbose llama.cpp logs — errors only
  llama_log_set([](enum ggml_log_level level, const char* text, void*) {
    if (level >= GGML_LOG_LEVEL_ERROR) std::cerr << text;
  }, nullptr);

  // Load the LLM model on CPU — the LlmEngine already has this model on GPU.
  // Avoids double GPU allocation that crashes on small VRAM cards.
  llama_model_params mparams = llama_model_default_params();
  mparams.n_gpu_layers = 0;

  model_ = llama_model_load_from_file(model_path.c_str(), mparams);
  if (!model_) {
    throw std::runtime_error("Failed to load vision model: " + model_path);
  }

  // Create context
  llama_context_params cparams = llama_context_default_params();
  cparams.n_ctx = n_ctx_;
  cparams.n_batch = 512;

  ctx_ = llama_init_from_model(model_, cparams);
  if (!ctx_) {
    llama_model_free(model_);
    model_ = nullptr;
    throw std::runtime_error("Failed to create vision context");
  }

  // Load mmproj on CPU only — it's a single large tensor (~781 MB)
  // that can't be split across GPU/CPU and often exceeds VRAM

  // Load multimodal projector (mmproj) for vision encoding
  // Try the standard naming pattern from HuggingFace
  std::string mmproj_path;
  auto slash_pos = model_path.rfind('/');
  if (slash_pos == std::string::npos) slash_pos = model_path.rfind('\\');
  std::string model_dir = (slash_pos != std::string::npos)
      ? model_path.substr(0, slash_pos + 1) : "";

  // Try exact HuggingFace naming: mmproj-Qwen3VL-2B-Instruct-F16.gguf
  if (!model_dir.empty()) {
    mmproj_path = model_dir + "mmproj-Qwen3VL-2B-Instruct-F16.gguf";
    if (!std::filesystem::exists(mmproj_path)) {
      // Try Q8_0 variant
      mmproj_path = model_dir + "mmproj-Qwen3VL-2B-Instruct-Q8_0.gguf";
    }
    if (!std::filesystem::exists(mmproj_path)) {
      // Try generic naming
      mmproj_path = model_dir + "mmproj-model-f16.gguf";
    }
    if (!std::filesystem::exists(mmproj_path)) {
      // Try deriving from model filename
      auto dot_pos = model_path.rfind('.');
      if (dot_pos != std::string::npos) {
        mmproj_path = model_path.substr(0, dot_pos) + "-mmproj-f16.gguf";
      }
    }
  }

  if (!mmproj_path.empty() && std::filesystem::exists(mmproj_path)) {
    auto mtmd_params = mtmd_context_params_default();

    // mmproj needs ~800 MB. Require 2 GB free to account for
    // LLM already consuming VRAM + KV cache + driver overhead.
    constexpr size_t kMmprojVramRequired = 2048ULL * 1024 * 1024;  // 2 GB
    bool gpu_has_space = false;

    if (device != "cpu") {
      size_t n_devices = ggml_backend_dev_count();
      for (size_t i = 0; i < n_devices; i++) {
        auto* dev = ggml_backend_dev_get(i);
        auto type = ggml_backend_dev_type(dev);
        if (type == GGML_BACKEND_DEVICE_TYPE_GPU) {
          size_t free_mem = 0, total_mem = 0;
          ggml_backend_dev_memory(dev, &free_mem, &total_mem);
          std::cerr << "[vision] GPU VRAM: " << (free_mem / 1024 / 1024) 
                    << " MB free / " << (total_mem / 1024 / 1024) << " MB total\n";
          if (free_mem >= kMmprojVramRequired) {
            gpu_has_space = true;
          }
          break;
        }
      }
    }

    mtmd_params.use_gpu = gpu_has_space;
    std::cerr << "[vision] mmproj: loading on "
              << (gpu_has_space ? "GPU" : "CPU") << "\n";
    mtmd_ctx_ = mtmd_init_from_file(mmproj_path.c_str(), model_, mtmd_params);
  }

  if (!mtmd_ctx_) {
    std::cerr << "[vision] Warning: mmproj model not found. "
              << "Vision features will not work. Text-only mode available.\n";
  }

  // Initialize sampler chain
  auto sparams = llama_sampler_chain_default_params();
  sampler_ = llama_sampler_chain_init(sparams);
  llama_sampler_chain_add(sampler_, llama_sampler_init_temp(0.6f));
  llama_sampler_chain_add(sampler_, llama_sampler_init_top_p(0.9f, 1));
  llama_sampler_chain_add(sampler_, llama_sampler_init_min_p(0.05f, 1));
  llama_sampler_chain_add(sampler_, llama_sampler_init_dist(LLAMA_DEFAULT_SEED));
}

VisionEngine::~VisionEngine() {
  if (sampler_) llama_sampler_free(sampler_);
  if (ctx_) llama_free(ctx_);
  if (model_) llama_model_free(model_);
  if (mtmd_ctx_) mtmd_free(mtmd_ctx_);
}

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

void VisionEngine::ResetContext() {
  llama_memory_clear(llama_get_memory(ctx_), true);
  llama_sampler_reset(sampler_);
}

std::string VisionEngine::RunVisionGeneration(
    const std::string& prompt,
    const std::vector<std::string>& image_paths,
    int max_length,
    TokenCallback on_token) {

  ResetContext();

  const auto* vocab = llama_model_get_vocab(model_);

  // Build prompt text with image markers
  std::string full_prompt;
  const char* marker = mtmd_ctx_ ? mtmd_default_marker() : "";

  for (size_t i = 0; i < image_paths.size(); i++) {
    full_prompt += marker;
  }
  full_prompt += prompt;

  // Apply chat template
  llama_chat_message msgs[] = {
      {"user", full_prompt.c_str()},
  };

  const char* chat_tmpl = llama_model_chat_template(model_, nullptr);
  int len = llama_chat_apply_template(
      chat_tmpl, msgs, 1, true, nullptr, 0);
  if (len < 0) {
    throw std::runtime_error("Failed to apply chat template for vision");
  }

  std::vector<char> tbuf(static_cast<size_t>(len) + 1);
  llama_chat_apply_template(
      chat_tmpl, msgs, 1, true, tbuf.data(), static_cast<int32_t>(tbuf.size()));
  std::string formatted(tbuf.data(), static_cast<size_t>(len));

  // If we have images and mtmd context, use multimodal pipeline
  if (mtmd_ctx_ && !image_paths.empty()) {
    // Load images as bitmaps
    std::vector<mtmd_bitmap*> bitmaps;
    for (const auto& img_path : image_paths) {
      auto* bmp = mtmd_helper_bitmap_init_from_file(mtmd_ctx_, img_path.c_str());
      if (bmp) {
        bitmaps.push_back(bmp);
      } else {
        std::cerr << "[vision] Warning: Failed to load image: " << img_path << "\n";
      }
    }

    // Tokenize with multimodal support
    mtmd_input_text text_input;
    text_input.text       = formatted.c_str();
    text_input.add_special = true;
    text_input.parse_special = true;

    auto* chunks = mtmd_input_chunks_init();
    std::vector<const mtmd_bitmap*> cbitmaps(bitmaps.begin(), bitmaps.end());
    int32_t ret = mtmd_tokenize(mtmd_ctx_, chunks, &text_input,
                                cbitmaps.data(), cbitmaps.size());

    if (ret != 0) {
      mtmd_input_chunks_free(chunks);
      for (auto* b : bitmaps) mtmd_bitmap_free(b);
      throw std::runtime_error("Failed to tokenize multimodal input");
    }

    // Evaluate all chunks (text + image embeddings)
    llama_pos new_n_past = 0;
    ret = mtmd_helper_eval_chunks(mtmd_ctx_, ctx_, chunks, 0, 0,
                                  llama_n_batch(ctx_), true, &new_n_past);

    mtmd_input_chunks_free(chunks);
    for (auto* b : bitmaps) mtmd_bitmap_free(b);

    if (ret != 0) {
      throw std::runtime_error("Failed to evaluate multimodal chunks");
    }
  } else {
    // Text-only: standard tokenization
    const int n_prompt_max = static_cast<int>(formatted.size()) + 256;
    std::vector<llama_token> tokens(n_prompt_max);
    const int n_prompt = llama_tokenize(
        vocab,
        formatted.c_str(),
        static_cast<int32_t>(formatted.size()),
        tokens.data(),
        static_cast<int32_t>(tokens.size()),
        true, true);

    if (n_prompt < 0) {
      throw std::runtime_error("Failed to tokenize vision prompt");
    }
    tokens.resize(static_cast<size_t>(n_prompt));

    const int n_batch = llama_n_batch(ctx_);
    for (int i = 0; i < n_prompt; i += n_batch) {
      int n_eval = std::min(n_batch, n_prompt - i);
      llama_batch batch = llama_batch_get_one(tokens.data() + i, n_eval);
      if (llama_decode(ctx_, batch) != 0) {
        throw std::runtime_error("Failed to decode vision prompt");
      }
    }
  }

  // Generate tokens
  std::string result;
  int n_generated = 0;

  while (n_generated < max_length) {
    llama_token token = llama_sampler_sample(sampler_, ctx_, -1);

    if (llama_token_is_eog(vocab, token)) {
      break;
    }

    char buf[256];
    int n = llama_token_to_piece(vocab, token, buf, sizeof(buf), 0, true);
    if (n > 0) {
      std::string piece(buf, static_cast<size_t>(n));
      result += piece;
      if (on_token) {
        on_token(piece);
      }
    }

    llama_batch next_batch = llama_batch_get_one(&token, 1);
    if (llama_decode(ctx_, next_batch) != 0) {
      break;
    }

    n_generated++;
  }

  return result;
}

}  // namespace EDGESCRIBE::vision
