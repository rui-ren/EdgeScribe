// EDGESCRIBE — LLM Engine Implementation
// Uses llama.cpp for streaming text generation with GGUF models

#include "llm/llm_engine.h"

#include <llama.h>
#include <ggml-backend.h>

#include <chrono>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <vector>

namespace EDGESCRIBE::llm {

// Suppress llama.cpp verbose logs — errors only
static void LlamaLogCallback(enum ggml_log_level level, const char* text, void*) {
  if (level >= GGML_LOG_LEVEL_ERROR) {
    std::cerr << text;
  }
}

static int DetectGpuLayers(const std::string& device) {
  if (device == "cpu") return 0;

  // For any other value (empty, "auto", "vulkan", "cuda", "metal"),
  // set n_gpu_layers = 999. llama.cpp will:
  //   - Offload all layers to GPU if a GPU backend is available
  //   - Silently fall back to CPU if no GPU backend is found
  // This is safe because llama.cpp never crashes on missing GPU.
  return 999;
}

LlmEngine::LlmEngine(const std::string& model_path,
                     const std::string& device)
    : model_path_(model_path) {
  llama_log_set(LlamaLogCallback, nullptr);

  llama_model_params mparams = llama_model_default_params();
  mparams.n_gpu_layers = DetectGpuLayers(device);

  model_ = llama_model_load_from_file(model_path.c_str(), mparams);

  // If GPU load fails (VRAM too small), retry with CPU
  if (!model_ && mparams.n_gpu_layers > 0) {
    std::cerr << "[llm] GPU memory insufficient, falling back to CPU\n";
    mparams.n_gpu_layers = 0;
    model_ = llama_model_load_from_file(model_path.c_str(), mparams);
  }

  if (!model_) {
    throw std::runtime_error("Failed to load llama model: " + model_path);
  }

  llama_context_params cparams = llama_context_default_params();
  cparams.n_ctx = n_ctx_;
  cparams.n_batch = 512;

  ctx_ = llama_init_from_model(model_, cparams);
  if (!ctx_) {
    llama_model_free(model_);
    model_ = nullptr;
    throw std::runtime_error("Failed to create llama context");
  }

  // Initialize sampler chain
  auto sparams = llama_sampler_chain_default_params();
  sampler_ = llama_sampler_chain_init(sparams);
  llama_sampler_chain_add(sampler_, llama_sampler_init_temp(0.6f));
  llama_sampler_chain_add(sampler_, llama_sampler_init_top_p(0.9f, 1));
  llama_sampler_chain_add(sampler_, llama_sampler_init_min_p(0.05f, 1));
  llama_sampler_chain_add(sampler_, llama_sampler_init_dist(LLAMA_DEFAULT_SEED));
}

LlmEngine::~LlmEngine() {
  if (sampler_) llama_sampler_free(sampler_);
  if (ctx_) llama_free(ctx_);
  if (model_) llama_model_free(model_);
}

std::string LlmEngine::Generate(const std::string& prompt,
                                int max_length,
                                TokenCallback on_token) {
  return RunGeneration(prompt, max_length, on_token);
}

std::string LlmEngine::Chat(const std::string& system_prompt,
                            const std::string& user_message,
                            int max_length,
                            TokenCallback on_token) {
  std::vector<ChatMessage> messages = {
      {"system", system_prompt},
      {"user", user_message},
  };
  return Chat(messages, max_length, on_token);
}

std::string LlmEngine::Chat(const std::vector<ChatMessage>& messages,
                            int max_length,
                            TokenCallback on_token) {
  std::string formatted = FormatChatMessages(messages);
  return RunGeneration(formatted, max_length, on_token);
}

std::string LlmEngine::GenerateSOAPNotes(const std::string& transcript,
                                         TokenCallback on_token) {
  return Chat(
      "You are a medical scribe assistant. Convert the following doctor-patient "
      "conversation transcript into structured SOAP notes. Use standard medical "
      "abbreviations. Be concise and accurate.",
      transcript,
      4096,
      on_token);
}

std::string LlmEngine::Summarize(const std::string& text,
                                 TokenCallback on_token) {
  return Chat(
      "You are a helpful assistant. Provide a concise summary of the following text.",
      text,
      2048,
      on_token);
}

std::string LlmEngine::FixMedicalTerms(const std::string& transcript,
                                       TokenCallback on_token) {
  return Chat(
      "You are a medical transcription editor. Fix any medical terminology errors "
      "in the following transcript. Only correct medical terms — do not change "
      "anything else. Return the corrected transcript.",
      transcript,
      4096,
      on_token);
}

std::string LlmEngine::FormatChatMessages(const std::vector<ChatMessage>& messages) {
  std::vector<llama_chat_message> llama_msgs;
  llama_msgs.reserve(messages.size());
  for (const auto& msg : messages) {
    llama_msgs.push_back({msg.role.c_str(), msg.content.c_str()});
  }

  const char* chat_tmpl = llama_model_chat_template(model_, nullptr);

  int len = llama_chat_apply_template(
      chat_tmpl,
      llama_msgs.data(),
      llama_msgs.size(),
      true,
      nullptr,
      0);

  if (len < 0) {
    throw std::runtime_error("Failed to apply chat template");
  }

  std::vector<char> buf(static_cast<size_t>(len) + 1);
  llama_chat_apply_template(
      chat_tmpl,
      llama_msgs.data(),
      llama_msgs.size(),
      true,
      buf.data(),
      static_cast<int32_t>(buf.size()));

  return std::string(buf.data(), static_cast<size_t>(len));
}

int LlmEngine::FindCommonPrefix(const std::vector<int>& cached,
                                const std::vector<int>& current) {
  int max_len = std::min(static_cast<int>(cached.size()),
                         static_cast<int>(current.size()));
  for (int i = 0; i < max_len; i++) {
    if (cached[i] != current[i]) return i;
  }
  return max_len;
}

void LlmEngine::InvalidateCache() {
  cache_valid_ = false;
  cached_tokens_.clear();
  llama_memory_clear(llama_get_memory(ctx_), true);
}

std::string LlmEngine::RunGeneration(const std::string& formatted_prompt,
                                     int max_length,
                                     TokenCallback on_token) {
  const auto* vocab = llama_model_get_vocab(model_);

  // Tokenize the full prompt
  const int n_prompt_max = static_cast<int>(formatted_prompt.size()) + 256;
  std::vector<llama_token> tokens(n_prompt_max);
  const int n_prompt = llama_tokenize(
      vocab,
      formatted_prompt.c_str(),
      static_cast<int32_t>(formatted_prompt.size()),
      tokens.data(),
      static_cast<int32_t>(tokens.size()),
      true,
      true);

  if (n_prompt < 0) {
    throw std::runtime_error("Failed to tokenize prompt");
  }
  tokens.resize(static_cast<size_t>(n_prompt));

  // KV cache prefix matching — find what's already cached
  std::vector<int> token_ids(tokens.begin(), tokens.end());
  int prefix_len = 0;

  if (cache_valid_) {
    prefix_len = FindCommonPrefix(cached_tokens_, token_ids);

    if (prefix_len >= n_prompt) {
      // Exact same prompt — trim old response tokens, keep prompt KV cached
      llama_memory_seq_rm(llama_get_memory(ctx_), 0,
                          static_cast<llama_pos>(n_prompt), -1);
      prefix_len = n_prompt;  // All prompt tokens are cached
    } else if (prefix_len < static_cast<int>(cached_tokens_.size())) {
      // Cache has diverged — remove tokens after the common prefix
      llama_memory_seq_rm(llama_get_memory(ctx_), 0,
                          static_cast<llama_pos>(prefix_len), -1);
    }
  } else {
    llama_memory_clear(llama_get_memory(ctx_), true);
    prefix_len = 0;
  }

  llama_sampler_reset(sampler_);

  int n_new = n_prompt - prefix_len;

  // Check context size
  if (n_prompt + max_length > n_ctx_) {
    max_length = n_ctx_ - n_prompt;
    if (max_length <= 0) {
      InvalidateCache();
      throw std::runtime_error("Prompt too long for context window");
    }
  }

  // Process ONLY new tokens (skip cached prefix)
  auto t_prompt_start = std::chrono::steady_clock::now();
  const int n_batch = llama_n_batch(ctx_);

  if (n_new == 0) {
    // All prompt tokens cached — re-decode last token to set sampling position
    llama_batch batch = llama_batch_get_one(&tokens[n_prompt - 1], 1);
    // Remove last token from KV first, then re-decode it
    llama_memory_seq_rm(llama_get_memory(ctx_), 0,
                        static_cast<llama_pos>(n_prompt - 1), -1);
    if (llama_decode(ctx_, batch) != 0) {
      InvalidateCache();
      throw std::runtime_error("Failed to decode last prompt token");
    }
  } else {
    for (int i = prefix_len; i < n_prompt; i += n_batch) {
      int n_eval = std::min(n_batch, n_prompt - i);
      llama_batch batch = llama_batch_get_one(tokens.data() + i, n_eval);
      if (llama_decode(ctx_, batch) != 0) {
        InvalidateCache();
        throw std::runtime_error("Failed to decode prompt batch");
      }
    }
  }
  auto t_first_token = std::chrono::steady_clock::now();

  // Generate tokens
  std::string result;
  std::vector<llama_token> generated_tokens;
  int n_generated = 0;

  while (n_generated < max_length) {
    llama_token token = llama_sampler_sample(sampler_, ctx_, -1);

    if (llama_vocab_is_eog(vocab, token)) {
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

    generated_tokens.push_back(token);

    llama_batch next_batch = llama_batch_get_one(&token, 1);
    if (llama_decode(ctx_, next_batch) != 0) {
      break;
    }

    n_generated++;
  }

  auto t_done = std::chrono::steady_clock::now();

  // Update cache state — prompt + generated tokens are now in KV cache
  cached_tokens_ = token_ids;
  for (llama_token t : generated_tokens) {
    cached_tokens_.push_back(static_cast<int>(t));
  }
  cache_valid_ = true;

  // Performance stats with cache info
  double ttft_ms = std::chrono::duration<double, std::milli>(
      t_first_token - t_prompt_start).count();
  double gen_ms = std::chrono::duration<double, std::milli>(
      t_done - t_first_token).count();
  double tok_per_sec = (gen_ms > 0 && n_generated > 0)
      ? (n_generated / (gen_ms / 1000.0)) : 0;
  double prompt_per_sec = (ttft_ms > 0 && n_new > 0)
      ? (n_new / (ttft_ms / 1000.0)) : 0;

  std::cerr << "\n\n"
            << "prompt: " << n_prompt << " tokens"
            << " | cached: " << prefix_len
            << " | new: " << n_new
            << " | " << std::fixed << std::setprecision(1)
            << prompt_per_sec << " tok/s"
            << " | TTFT: " << ttft_ms << " ms\n"
            << "output: " << n_generated << " tokens | "
            << tok_per_sec << " tok/s | "
            << gen_ms << " ms\n";

  return result;
}

}  // namespace EDGESCRIBE::llm
