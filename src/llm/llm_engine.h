// EDGESCRIBE — LLM Engine
// Wraps onnxruntime-genai for text generation (chat, SOAP notes, summarization)

#pragma once

#include <functional>
#include <memory>
#include <string>
#include <vector>

// Forward declarations
struct OgaModel;
struct OgaTokenizer;
struct OgaTokenizerStream;

namespace EDGESCRIBE::llm {

// Callback for streaming token output
using TokenCallback = std::function<void(const std::string& token)>;

struct ChatMessage {
  std::string role;     // "system", "user", "assistant"
  std::string content;
};

class LlmEngine {
 public:
  explicit LlmEngine(const std::string& model_path,
                     const std::string& device = "cpu");
  ~LlmEngine();

  // Generate text from a raw prompt (no chat template)
  std::string Generate(const std::string& prompt,
                       int max_length = 2048,
                       TokenCallback on_token = nullptr);

  // Single-turn chat with system + user message
  std::string Chat(const std::string& system_prompt,
                   const std::string& user_message,
                   int max_length = 2048,
                   TokenCallback on_token = nullptr);

  // Multi-turn chat with message history
  std::string Chat(const std::vector<ChatMessage>& messages,
                   int max_length = 2048,
                   TokenCallback on_token = nullptr);

  // Generate SOAP notes from a transcript
  std::string GenerateSOAPNotes(const std::string& transcript,
                                TokenCallback on_token = nullptr);

  // Summarize text
  std::string Summarize(const std::string& text,
                        TokenCallback on_token = nullptr);

  // Fix medical terminology in transcript
  std::string FixMedicalTerms(const std::string& transcript,
                              TokenCallback on_token = nullptr);

 private:
  std::string RunGeneration(const std::string& formatted_prompt,
                            int max_length,
                            TokenCallback on_token);

  std::string FormatChatMessages(const std::vector<ChatMessage>& messages);

  std::unique_ptr<OgaModel> model_;
  std::unique_ptr<OgaTokenizer> tokenizer_;
  std::string model_path_;
};

}  // namespace EDGESCRIBE::llm
