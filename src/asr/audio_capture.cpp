// EDGESCRIBE — Audio capture implementation using miniaudio

#define MINIAUDIO_IMPLEMENTATION
#include "miniaudio.h"

#include "asr/audio_capture.h"
#include <iostream>
#include <stdexcept>

namespace EDGESCRIBE {

struct AudioCapture::Impl {
  ma_device device;
  ma_device_config config;
  bool device_initialized = false;
};

AudioCapture::AudioCapture() : impl_(std::make_unique<Impl>()) {}

AudioCapture::~AudioCapture() {
  Stop();
  if (impl_->device_initialized) {
    ma_device_uninit(&impl_->device);
  }
}

void AudioCapture::DataCallback(void* device_ptr, void* output,
                                const void* input,
                                unsigned int frame_count) {
  (void)output;  // Capture only, no playback
  auto* device = static_cast<ma_device*>(device_ptr);
  auto* capture = static_cast<AudioCapture*>(device->pUserData);

  if (capture && capture->capturing_.load() && capture->callback_) {
    const float* samples = static_cast<const float*>(input);
    capture->callback_(samples, static_cast<size_t>(frame_count));
  }
}

void AudioCapture::Start(AudioCallback callback) {
  if (capturing_.load()) {
    return;  // Already capturing
  }

  callback_ = std::move(callback);

  impl_->config = ma_device_config_init(ma_device_type_capture);
  impl_->config.capture.format = ma_format_f32;
  impl_->config.capture.channels = 1;       // Mono
  impl_->config.sampleRate = 16000;          // 16kHz — required by ASR models
  impl_->config.dataCallback = [](ma_device* device, void* output,
                                  const void* input, ma_uint32 frame_count) {
    AudioCapture::DataCallback(device, output, input, frame_count);
  };
  impl_->config.pUserData = this;

  if (impl_->device_initialized) {
    ma_device_uninit(&impl_->device);
    impl_->device_initialized = false;
  }

  ma_result result = ma_device_init(nullptr, &impl_->config, &impl_->device);
  if (result != MA_SUCCESS) {
    throw std::runtime_error("Failed to initialize audio capture device (error: " +
                             std::to_string(result) + ")");
  }
  impl_->device_initialized = true;

  result = ma_device_start(&impl_->device);
  if (result != MA_SUCCESS) {
    ma_device_uninit(&impl_->device);
    impl_->device_initialized = false;
    throw std::runtime_error("Failed to start audio capture (error: " +
                             std::to_string(result) + ")");
  }

  capturing_.store(true);
}

void AudioCapture::Stop() {
  if (!capturing_.load()) {
    return;
  }

  capturing_.store(false);

  if (impl_->device_initialized) {
    ma_device_stop(&impl_->device);
  }
}

std::vector<std::string> AudioCapture::ListDevices() {
  std::vector<std::string> devices;

  ma_context context;
  ma_result result = ma_context_init(nullptr, 0, nullptr, &context);
  if (result != MA_SUCCESS) {
    return devices;
  }

  ma_device_info* capture_infos;
  ma_uint32 capture_count;
  result = ma_context_get_devices(&context, nullptr, nullptr,
                                  &capture_infos, &capture_count);
  if (result == MA_SUCCESS) {
    for (ma_uint32 i = 0; i < capture_count; i++) {
      devices.emplace_back(capture_infos[i].name);
    }
  }

  ma_context_uninit(&context);
  return devices;
}

}  // namespace EDGESCRIBE
