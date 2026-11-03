#include "../Posix/PosixServices.hpp"

#include <AudioToolbox/AudioToolbox.h>
#include <algorithm>
#include <array>
#include <condition_variable>
#include <cstdlib>
#include <cstring>
#include <mach-o/dyld.h>
#include <mutex>
#include <stdexcept>

namespace termify::platform {
namespace {

std::filesystem::path homeDirectory() {
  const char *home = std::getenv("HOME");
  if (!home || !*home || !std::filesystem::path(home).is_absolute())
    throw std::runtime_error("HOME is required for macOS storage");
  return home;
}

class MacOSFileSystem final : public posix::FileSystem {
public:
  std::filesystem::path ExecutableDirectory() const override {
    std::uint32_t size = 0;
    _NSGetExecutablePath(nullptr, &size);
    std::vector<char> path(size);
    if (_NSGetExecutablePath(path.data(), &size) != 0)
      throw std::runtime_error("Failed to locate the Termify executable");
    return std::filesystem::weakly_canonical(path.data()).parent_path();
  }

  std::filesystem::path
  UserDataDirectory(const std::string &application) const override {
    return homeDirectory() / "Library/Application Support" / application;
  }

  std::filesystem::path
  UserCacheDirectory(const std::string &application) const override {
    return homeDirectory() / "Library/Caches" / application;
  }

protected:
  std::vector<std::filesystem::path>
  SystemExecutableCandidates(const std::string &name) const override {
    return {std::filesystem::path("/opt/homebrew/bin") / name,
            std::filesystem::path("/usr/local/bin") / name};
  }
};

class CoreAudioOutput final : public AudioOutput {
public:
  explicit CoreAudioOutput(const AudioOutputConfig &config)
      : _channels(config.channels) {
    AudioStreamBasicDescription format{};
    format.mSampleRate = config.sampleRate;
    format.mFormatID = kAudioFormatLinearPCM;
    format.mFormatFlags = kAudioFormatFlagIsSignedInteger |
                          kAudioFormatFlagIsPacked |
                          kAudioFormatFlagsNativeEndian;
    format.mBytesPerPacket = sizeof(std::int16_t) * config.channels;
    format.mFramesPerPacket = 1;
    format.mBytesPerFrame = format.mBytesPerPacket;
    format.mChannelsPerFrame = config.channels;
    format.mBitsPerChannel = 16;

    check(AudioQueueNewOutput(&format, &CoreAudioOutput::bufferFinished, this,
                              nullptr, nullptr, 0, &_queue),
          "Could not create a Core Audio output queue");
    try {
      for (auto &slot : _buffers) {
        check(AudioQueueAllocateBuffer(_queue, BUFFER_BYTES, &slot.buffer),
              "Could not allocate a Core Audio buffer");
        slot.available = true;
      }
    } catch (...) {
      AudioQueueDispose(_queue, true);
      _queue = nullptr;
      throw;
    }
  }

  ~CoreAudioOutput() override {
    if (_queue) {
      AudioQueueStop(_queue, true);
      AudioQueueDispose(_queue, true);
    }
  }

  void Reset() override {
    if (_started)
      check(AudioQueueStop(_queue, true), "Could not stop Core Audio output");
    {
      std::lock_guard lock(_mutex);
      for (auto &slot : _buffers)
        slot.available = true;
    }
    _available.notify_all();
    check(AudioQueueReset(_queue), "Could not reset Core Audio output");
    _started = false;
  }

  std::size_t WriteFrames(const std::int16_t *samples,
                          std::size_t frames) override {
    const std::size_t capacity =
        BUFFER_BYTES / (sizeof(std::int16_t) * _channels);
    frames = std::min(frames, capacity);

    BufferSlot *slot = nullptr;
    {
      std::unique_lock lock(_mutex);
      _available.wait(lock, [this] {
        return std::any_of(_buffers.begin(), _buffers.end(),
                           [](const auto &candidate) {
                             return candidate.available;
                           });
      });
      slot = &*std::find_if(_buffers.begin(), _buffers.end(),
                            [](const auto &candidate) {
                              return candidate.available;
                            });
      slot->available = false;
    }

    const auto bytes = frames * sizeof(std::int16_t) * _channels;
    std::memcpy(slot->buffer->mAudioData, samples, bytes);
    slot->buffer->mAudioDataByteSize = static_cast<UInt32>(bytes);
    const OSStatus result = AudioQueueEnqueueBuffer(_queue, slot->buffer, 0,
                                                    nullptr);
    if (result != noErr) {
      std::lock_guard lock(_mutex);
      slot->available = true;
      _available.notify_one();
      return 0;
    }
    if (!_started) {
      check(AudioQueueStart(_queue, nullptr),
            "Could not start Core Audio output");
      _started = true;
    }
    return frames;
  }

private:
  struct BufferSlot {
    AudioQueueBufferRef buffer{nullptr};
    bool available{false};
  };

  static constexpr UInt32 BUFFER_BYTES = 16384;

  static void bufferFinished(void *context, AudioQueueRef,
                             AudioQueueBufferRef buffer) {
    auto &output = *static_cast<CoreAudioOutput *>(context);
    std::lock_guard lock(output._mutex);
    const auto slot = std::find_if(
        output._buffers.begin(), output._buffers.end(),
        [buffer](const auto &candidate) { return candidate.buffer == buffer; });
    if (slot != output._buffers.end()) {
      slot->available = true;
      output._available.notify_one();
    }
  }

  static void check(OSStatus status, const char *message) {
    if (status != noErr)
      throw std::runtime_error(message);
  }

  int _channels;
  AudioQueueRef _queue{nullptr};
  bool _started{false};
  std::array<BufferSlot, 3> _buffers{};
  std::mutex _mutex;
  std::condition_variable _available;
};

class MacOSPlatform final : public PlatformServices {
public:
  platform::ProcessRunner &Processes() override { return _processes; }
  platform::FileSystem &Files() override { return _files; }

  std::unique_ptr<TerminalDriver>
  CreateTerminal(int inputDescriptor, int outputDescriptor) override {
    return std::make_unique<posix::TerminalDriver>(inputDescriptor,
                                                   outputDescriptor);
  }

  std::unique_ptr<AudioOutput>
  CreateAudioOutput(const AudioOutputConfig &config) override {
    return std::make_unique<CoreAudioOutput>(config);
  }

  void InstallInterruptHandler() override {
    posix::InstallInterruptHandler();
  }

private:
  posix::ProcessRunner _processes;
  MacOSFileSystem _files;
};

} // namespace

PlatformServices &CurrentPlatform() {
  static MacOSPlatform platform;
  return platform;
}

} // namespace termify::platform
