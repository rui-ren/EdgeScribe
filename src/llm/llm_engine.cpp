// EDGESCRIBE — LLM Engine Implementation
// Uses onnxruntime-genai for streaming text generation

#include "llm/llm_engine.h"
#include "ort_genai.h"
#include <iostream>
#include <sstream>

namespace EDGESCRIBE::llm {

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

LlmEngine::LlmEngine(const std::string& model_path,
                     const std::string& device)
    : model_path_(model_path) {
  model_ = CreateModelWithDevice(model_path, device);
  tokenizer_ = OgaTokenizer::Create(*model_);
}

LlmEngine::~LlmEngine() = default;

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
  // Build JSON messages array for chat template
  std::ostringstream json;
  json << "[";
  for (size_t i = 0; i < messages.size(); i++) {
    if (i > 0) json << ",";
    json << "{\"role\":\"" << messages[i].role << "\","
         << "\"content\":\"";
    // Escape special characters in content
    for (char c : messages[i].content) {
      switch (c) {
        case '"': json << "\\\""; break;
        case '\\': json << "\\\\"; break;
        case '\n': json << "\\n"; break;
        case '\r': json << "\\r"; break;
        case '\t': json << "\\t"; break;
        default: json << c; break;
      }
    }
    json << "\"}";
  }
  json << "]";

  // Apply the model's chat template
  OgaString result = tokenizer_->ApplyChatTemplate(
      nullptr,  // Use model's default template
      json.str().c_str(),
      nullptr,  // No tools
      true      // Add generation prompt
  );

  return std::string(static_cast<const char*>(result));
}

std::string LlmEngine::RunGeneration(const std::string& formatted_prompt,
                                     int max_length,
                                     TokenCallback on_token) {
  auto params = OgaGeneratorParams::Create(*model_);
  params->SetSearchOption("max_length", static_cast<double>(max_length));

  auto generator = OgaGenerator::Create(*model_, *params);
  auto token_stream = OgaTokenizerStream::Create(*tokenizer_);

  // Encode prompt and feed to generator
  auto sequences = OgaSequences::Create();
  tokenizer_->Encode(formatted_prompt.c_str(), *sequences);
  generator->AppendTokenSequences(*sequences);

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

}  // namespace EDGESCRIBE::llm
