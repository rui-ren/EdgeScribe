// EDGESCRIBE — Phonemizer Implementation
// Dynamically loads espeak-ng at runtime for G2P conversion
//
// espeak-ng is the industry standard for text-to-phoneme conversion.
// It supports 100+ languages and outputs IPA phonemes.
//
// Install espeak-ng:
//   Windows: choco install espeak-ng  OR  download from https://github.com/espeak-ng/espeak-ng/releases
//   macOS:   brew install espeak-ng
//   Linux:   sudo apt install espeak-ng libespeak-ng-dev
//
// If espeak-ng is not installed, falls back to a simple passthrough that
// sends raw text characters as phoneme tokens (reduced quality).

#include "tts/phonemizer.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <sstream>

#ifdef _WIN32
#include <windows.h>
#else
#include <dlfcn.h>
#include <unistd.h>
#endif

namespace fs = std::filesystem;

// espeak-ng constants (avoid header dependency)
#define ESPEAK_AUDIO_OUTPUT_RETRIEVAL 0x2000
#define ESPEAK_CHARS_AUTO 0
#define ESPEAK_PHONEMES_IPA 0x02

namespace {

std::string GetExeDir() {
#ifdef _WIN32
  char path[MAX_PATH];
  GetModuleFileNameA(nullptr, path, MAX_PATH);
  return fs::path(path).parent_path().string();
#else
  char path[4096];
  ssize_t len = readlink("/proc/self/exe", path, sizeof(path) - 1);
  if (len > 0) {
    path[len] = '\0';
    return fs::path(path).parent_path().string();
  }
  return ".";
#endif
}

}  // namespace

namespace EDGESCRIBE::tts {

Phonemizer::Phonemizer() {
  available_ = LoadEspeakLibrary();
  if (available_) {
    // Try initializing with bundled data directory first (next to exe)
    // espeak_Initialize(output, buflength, path, options)
    // path = nullptr means use default system locations
    int result = -1;

    // Try bundled espeak-ng-data next to the executable
    auto exe_dir = fs::path(GetExeDir());
    auto bundled_data = exe_dir / "espeak-ng-data";
    if (fs::exists(bundled_data)) {
      result = fn_init_(ESPEAK_AUDIO_OUTPUT_RETRIEVAL, 0,
                        bundled_data.parent_path().string().c_str(), 0);
    }

    // Fall back to system-installed espeak-ng
    if (result < 0) {
      result = fn_init_(ESPEAK_AUDIO_OUTPUT_RETRIEVAL, 0, nullptr, 0);
    }

    if (result >= 0) {
      initialized_ = true;
    } else {
      available_ = false;
    }
  }
}

Phonemizer::~Phonemizer() {
  if (initialized_ && fn_terminate_) {
    fn_terminate_();
  }
  if (lib_handle_) {
#ifdef _WIN32
    FreeLibrary(static_cast<HMODULE>(lib_handle_));
#else
    dlclose(lib_handle_);
#endif
  }
}

bool Phonemizer::LoadEspeakLibrary() {
#ifdef _WIN32
  // Try common espeak-ng DLL locations on Windows
  const char* lib_names[] = {
      "espeak-ng.dll",
      "libespeak-ng.dll",
      "C:\\Program Files\\eSpeak NG\\libespeak-ng.dll",
      "C:\\Program Files (x86)\\eSpeak NG\\libespeak-ng.dll",
  };

  HMODULE handle = nullptr;
  for (const auto& name : lib_names) {
    handle = LoadLibraryA(name);
    if (handle) break;
  }
  if (!handle) return false;
  lib_handle_ = handle;

  fn_init_ = reinterpret_cast<EspeakInitFn>(
      GetProcAddress(handle, "espeak_Initialize"));
  fn_set_voice_ = reinterpret_cast<EspeakSetVoiceByNameFn>(
      GetProcAddress(handle, "espeak_SetVoiceByName"));
  fn_text_to_phonemes_ = reinterpret_cast<EspeakTextToPhonemesFn>(
      GetProcAddress(handle, "espeak_TextToPhonemes"));
  fn_terminate_ = reinterpret_cast<EspeakTerminateFn>(
      GetProcAddress(handle, "espeak_Terminate"));

#else
  // Try common espeak-ng library locations on macOS/Linux
  const char* lib_names[] = {
#ifdef __APPLE__
      "libespeak-ng.dylib",
      "/opt/homebrew/lib/libespeak-ng.dylib",
      "/usr/local/lib/libespeak-ng.dylib",
#else
      "libespeak-ng.so",
      "libespeak-ng.so.1",
      "/usr/lib/x86_64-linux-gnu/libespeak-ng.so.1",
      "/usr/lib/aarch64-linux-gnu/libespeak-ng.so.1",
#endif
  };

  void* handle = nullptr;
  for (const auto& name : lib_names) {
    handle = dlopen(name, RTLD_LAZY);
    if (handle) break;
  }
  if (!handle) return false;
  lib_handle_ = handle;

  fn_init_ = reinterpret_cast<EspeakInitFn>(
      dlsym(handle, "espeak_Initialize"));
  fn_set_voice_ = reinterpret_cast<EspeakSetVoiceByNameFn>(
      dlsym(handle, "espeak_SetVoiceByName"));
  fn_text_to_phonemes_ = reinterpret_cast<EspeakTextToPhonemesFn>(
      dlsym(handle, "espeak_TextToPhonemes"));
  fn_terminate_ = reinterpret_cast<EspeakTerminateFn>(
      dlsym(handle, "espeak_Terminate"));
#endif

  // All required functions must be found
  return fn_init_ && fn_set_voice_ && fn_text_to_phonemes_;
}

std::string Phonemizer::TextToPhonemes(const std::string& text,
                                       const std::string& language) {
  if (!available_ || !initialized_) {
    return FallbackPhonemize(text);
  }

  // Set language/voice
  fn_set_voice_(language.c_str());

  // Convert text to IPA phonemes
  // espeak_TextToPhonemes processes the text pointer incrementally
  std::string result;
  const char* input = text.c_str();
  const void* input_ptr = static_cast<const void*>(input);

  while (input_ptr && *static_cast<const char*>(input_ptr) != '\0') {
    const char* phonemes = fn_text_to_phonemes_(
        &input_ptr, ESPEAK_CHARS_AUTO, ESPEAK_PHONEMES_IPA);
    if (phonemes && phonemes[0] != '\0') {
      if (!result.empty() && result.back() != ' ') {
        result += ' ';
      }
      result += phonemes;
    }
  }

  return result;
}

std::string Phonemizer::FallbackPhonemize(const std::string& text) {
  // Simple fallback: normalize text for the basic character tokenizer
  // This produces lower-quality TTS but at least works without espeak-ng
  std::string result;
  result.reserve(text.size());

  for (size_t i = 0; i < text.size(); i++) {
    char c = text[i];
    // Keep printable ASCII
    if (c >= 32 && c <= 126) {
      result += c;
    }
  }

  // Trim whitespace
  while (!result.empty() && result.back() == ' ') result.pop_back();
  while (!result.empty() && result.front() == ' ') result.erase(result.begin());

  return result;
}

}  // namespace EDGESCRIBE::tts
