// EDGESCRIBE — Kokoro Phoneme Vocabulary
// Maps IPA phoneme characters to Kokoro token IDs
//
// Kokoro TTS uses a phoneme-level vocabulary derived from IPA (International
// Phonetic Alphabet). This file defines the mapping from IPA symbols to the
// integer token IDs that the Kokoro ONNX model expects.
//
// The vocabulary is based on the kokoro-onnx project:
// https://github.com/thewh1teagle/kokoro-onnx
//
// Token 0 = pad, 1 = start/BOS, 2 = end/EOS
// Phoneme tokens start at 3

#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace EDGESCRIBE::tts {

// Kokoro phoneme vocabulary
// Maps IPA characters/digraphs to token IDs
inline const std::unordered_map<std::string, int64_t>& GetKokoroVocab() {
  static const std::unordered_map<std::string, int64_t> vocab = {
      // Special tokens
      {"<pad>", 0},
      {"<bos>", 1},
      {"<eos>", 2},

      // Whitespace and punctuation
      {" ", 3},
      {"!", 4},
      {"\"", 5},
      {"#", 6},
      {"$", 7},
      {"%", 8},
      {"&", 9},
      {"'", 10},
      {"(", 11},
      {")", 12},
      {"*", 13},
      {"+", 14},
      {",", 15},
      {"-", 16},
      {".", 17},
      {"/", 18},

      // Digits
      {"0", 19},
      {"1", 20},
      {"2", 21},
      {"3", 22},
      {"4", 23},
      {"5", 24},
      {"6", 25},
      {"7", 26},
      {"8", 27},
      {"9", 28},

      // Punctuation continued
      {":", 29},
      {";", 30},
      {"<", 31},
      {"=", 32},
      {">", 33},
      {"?", 34},
      {"@", 35},

      // IPA vowels (monophthongs)
      {"a", 36},
      {"b", 37},
      {"c", 38},
      {"d", 39},
      {"e", 40},
      {"f", 41},
      {"g", 42},
      {"h", 43},
      {"i", 44},
      {"j", 45},
      {"k", 46},
      {"l", 47},
      {"m", 48},
      {"n", 49},
      {"o", 50},
      {"p", 51},
      {"q", 52},
      {"r", 53},
      {"s", 54},
      {"t", 55},
      {"u", 56},
      {"v", 57},
      {"w", 58},
      {"x", 59},
      {"y", 60},
      {"z", 61},

      // IPA specific symbols
      {"\xC9\x91", 62},   // ɑ (open back unrounded)
      {"\xC3\xA6", 63},   // æ (near-open front unrounded)
      {"\xCA\x83", 64},   // ʃ (voiceless postalveolar fricative)
      {"\xCA\x92", 65},   // ʒ (voiced postalveolar fricative)
      {"\xC3\xB0", 66},   // ð (voiced dental fricative)
      {"\xCE\xB8", 67},   // θ (voiceless dental fricative)
      {"\xC9\xAA", 68},   // ɪ (near-close near-front unrounded)
      {"\xCA\x8A", 69},   // ʊ (near-close near-back rounded)
      {"\xC9\x9B", 70},   // ɛ (open-mid front unrounded)
      {"\xC9\x94", 71},   // ɔ (open-mid back rounded)
      {"\xC9\x99", 72},   // ə (schwa)
      {"\xCA\x8C", 73},   // ʌ (open-mid back unrounded)
      {"\xC5\x8B", 74},   // ŋ (velar nasal)
      {"\xC9\xB9", 75},   // ɹ (alveolar approximant)
      {"\xCA\x94", 76},   // ʔ (glottal stop)
      {"\xC9\xBE", 77},   // ɾ (alveolar flap)
      {"\xC9\xAB", 78},   // ɫ (dark l, velarized)
      {"\xC9\x9C", 79},   // ɜ (open-mid central unrounded)
      {"\xC9\x92", 80},   // ɒ (open back rounded)

      // IPA diacritics and suprasegmentals
      {"\xCB\x88", 81},   // ˈ (primary stress)
      {"\xCB\x8C", 82},   // ˌ (secondary stress)
      {"\xCB\x90", 83},   // ː (long)
      {"\xCC\xA9", 84},   // ̩ (syllabic)
      {"\xCC\xAF", 85},   // ̯ (non-syllabic)
      {"\xCA\xB0", 86},   // ʰ (aspirated)
      {"\xCB\xA1", 87},   // ˡ (lateral release)

      // Additional IPA
      {"\xC3\xA8", 88},   // è
      {"\xC9\xA8", 89},   // ɨ (close central unrounded)
      {"\xC3\xB8", 90},   // ø (close-mid front rounded)
      {"\xC3\xBC", 91},   // ü
      {"\xC3\xA3", 92},   // ã (nasalized a)
      {"\xC3\xB5", 93},   // õ (nasalized o)
      {"\xC4\xA9", 94},   // ĩ (nasalized i)
      {"\xC5\xA9", 95},   // ũ (nasalized u)

      // Tie bars and affricates
      {"\xCD\x9C", 96},   // ͜ (tie bar below)
      {"\xCD\xA1", 97},   // ͡ (tie bar above, used in t͡ʃ, d͡ʒ)
  };

  return vocab;
}

// Tokenize an IPA phoneme string into Kokoro token IDs
inline std::vector<int64_t> PhonemeStringToTokens(const std::string& phonemes) {
  const auto& vocab = GetKokoroVocab();
  std::vector<int64_t> tokens;
  tokens.push_back(1);  // BOS

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
      // Try single ASCII character
      std::string single(1, phonemes[i]);
      auto it = vocab.find(single);
      if (it != vocab.end()) {
        tokens.push_back(it->second);
      }
      // Skip unknown characters (including multi-byte UTF-8 continuations)
      if ((phonemes[i] & 0xC0) == 0xC0) {
        // Start of multi-byte: skip continuation bytes
        i++;
        while (i < phonemes.size() && (phonemes[i] & 0xC0) == 0x80) i++;
      } else {
        i++;
      }
    }
  }

  tokens.push_back(2);  // EOS
  return tokens;
}

}  // namespace EDGESCRIBE::tts
