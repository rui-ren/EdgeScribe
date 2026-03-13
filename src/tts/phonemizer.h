// EDGESCRIBE — Phonemizer
// Converts text to IPA phonemes using espeak-ng (dynamically loaded)
// Falls back to passthrough if espeak-ng is not installed

#pragma once

#include <string>
#include <vector>

namespace EDGESCRIBE::tts {

class Phonemizer {
 public:
  Phonemizer();
  ~Phonemizer();

  // Convert text to IPA phoneme string
  std::string TextToPhonemes(const std::string& text,
                             const std::string& language = "en-us");

  // Check if espeak-ng is available
  bool IsAvailable() const { return available_; }

  // Get backend name
  const char* Backend() const { return available_ ? "espeak-ng" : "fallback"; }

 private:
  bool available_ = false;
  bool initialized_ = false;

  // Dynamic library handle
  void* lib_handle_ = nullptr;

  // Function pointers (loaded dynamically)
  using EspeakInitFn = int (*)(int, int, const char*, int);
  using EspeakSetVoiceByNameFn = int (*)(const char*);
  using EspeakTextToPhonemesFn = const char* (*)(const void**, int, int);
  using EspeakTerminateFn = int (*)();

  EspeakInitFn fn_init_ = nullptr;
  EspeakSetVoiceByNameFn fn_set_voice_ = nullptr;
  EspeakTextToPhonemesFn fn_text_to_phonemes_ = nullptr;
  EspeakTerminateFn fn_terminate_ = nullptr;

  // Try to load espeak-ng dynamically
  bool LoadEspeakLibrary();

  // Simple fallback: basic English text normalization
  static std::string FallbackPhonemize(const std::string& text);
};

}  // namespace EDGESCRIBE::tts
