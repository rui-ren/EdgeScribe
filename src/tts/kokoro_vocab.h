// EDGESCRIBE — Kokoro Phoneme Vocabulary
// Loads IPA phoneme to token ID mapping from tokenizer.json at runtime.
// Falls back to a minimal hardcoded vocab if tokenizer.json is not found.

#pragma once

#include <cctype>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <unordered_map>
#include <vector>

namespace EDGESCRIBE::tts {

// Parse tokenizer.json to extract vocab: { "model": { "vocab": { "c": id } } }
inline std::unordered_map<std::string, int64_t> LoadTokenizerJson(
    const std::string& path) {
  std::unordered_map<std::string, int64_t> vocab;

  std::ifstream ifs(path);
  if (!ifs.is_open()) return vocab;

  std::string content((std::istreambuf_iterator<char>(ifs)),
                       std::istreambuf_iterator<char>());

  // Find "vocab": { ... }
  auto vocab_pos = content.find("\"vocab\"");
  if (vocab_pos == std::string::npos) return vocab;

  auto brace_start = content.find('{', vocab_pos);
  if (brace_start == std::string::npos) return vocab;

  size_t pos = brace_start + 1;
  int depth = 1;

  while (pos < content.size() && depth > 0) {
    // Skip whitespace and commas
    while (pos < content.size() && (content[pos] == ' ' || content[pos] == '\n' ||
           content[pos] == '\r' || content[pos] == '\t' || content[pos] == ','))
      pos++;

    if (pos >= content.size()) break;
    if (content[pos] == '}') { depth--; pos++; continue; }
    if (content[pos] == '{') { depth++; pos++; continue; }

    // Parse "key": value
    if (content[pos] != '"') { pos++; continue; }
    pos++;  // skip "

    std::string key;
    while (pos < content.size() && content[pos] != '"') {
      if (content[pos] == '\\' && pos + 1 < content.size()) {
        pos++;
        if (content[pos] == '"') key += '"';
        else if (content[pos] == '\\') key += '\\';
        else if (content[pos] == 'n') key += '\n';
        else if (content[pos] == 'u' && pos + 4 < content.size()) {
          std::string hex = content.substr(pos + 1, 4);
          unsigned long cp = std::stoul(hex, nullptr, 16);
          if (cp < 0x80) {
            key += static_cast<char>(cp);
          } else if (cp < 0x800) {
            key += static_cast<char>(0xC0 | (cp >> 6));
            key += static_cast<char>(0x80 | (cp & 0x3F));
          } else {
            key += static_cast<char>(0xE0 | (cp >> 12));
            key += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
            key += static_cast<char>(0x80 | (cp & 0x3F));
          }
          pos += 4;
        } else {
          key += content[pos];
        }
      } else {
        key += content[pos];
      }
      pos++;
    }
    if (pos < content.size()) pos++;  // skip closing "

    // Skip : and whitespace
    while (pos < content.size() && (content[pos] == ':' || content[pos] == ' '))
      pos++;

    // Parse integer value
    if (pos < content.size() && (content[pos] == '-' || std::isdigit(static_cast<unsigned char>(content[pos])))) {
      std::string num;
      while (pos < content.size() && (content[pos] == '-' || std::isdigit(static_cast<unsigned char>(content[pos])))) {
        num += content[pos++];
      }
      vocab[key] = std::stoll(num);
    }
  }

  return vocab;
}

// Get vocab — loads from tokenizer.json once, then caches
inline const std::unordered_map<std::string, int64_t>& GetKokoroVocab(
    const std::string& model_dir = "") {
  static std::unordered_map<std::string, int64_t> vocab;
  static bool loaded = false;

  if (!loaded && !model_dir.empty()) {
    namespace fs = std::filesystem;
    fs::path tok_path = fs::path(model_dir) / "tokenizer.json";
    if (fs::exists(tok_path)) {
      vocab = LoadTokenizerJson(tok_path.string());
      if (!vocab.empty()) {
        std::cout << "TTS vocab: " << vocab.size()
                  << " tokens from tokenizer.json" << std::endl;
        loaded = true;
      }
    }
  }

  // Minimal fallback if tokenizer.json not available
  if (vocab.empty()) {
    vocab = {
        {"$", 0}, {" ", 16}, {",", 3}, {".", 4}, {"!", 5}, {"?", 6},
        {"a", 43}, {"b", 44}, {"d", 46}, {"e", 47}, {"f", 48},
        {"h", 50}, {"i", 51}, {"k", 53}, {"l", 54}, {"m", 55},
        {"n", 56}, {"o", 57}, {"p", 58}, {"r", 60}, {"s", 61},
        {"t", 62}, {"u", 63}, {"v", 64}, {"w", 65}, {"z", 68},
    };
    loaded = true;
  }

  return vocab;
}

inline std::vector<int64_t> PhonemeStringToTokens(const std::string& phonemes) {
  const auto& vocab = GetKokoroVocab();
  std::vector<int64_t> tokens;
  tokens.push_back(0);  // Pad token '$' at start

  size_t i = 0;
  while (i < phonemes.size()) {
    bool found = false;

    // Try multi-byte UTF-8 sequences (up to 4 bytes)
    for (int len = 4; len >= 1; len--) {
      if (i + len > phonemes.size()) continue;

      std::string candidate = phonemes.substr(i, len);
      auto it = vocab.find(candidate);
      if (it != vocab.end()) {
        tokens.push_back(it->second);
        i += len;
        found = true;
        break;
      }
    }

    if (!found) {
      // Skip unknown characters
      if ((phonemes[i] & 0xC0) == 0xC0) {
        i++;
        while (i < phonemes.size() && (phonemes[i] & 0xC0) == 0x80) i++;
      } else {
        i++;
      }
    }
  }

  tokens.push_back(0);  // Pad token '$' at end
  return tokens;
}

}  // namespace EDGESCRIBE::tts
