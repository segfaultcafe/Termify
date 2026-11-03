#include "../Posix/PosixServices.hpp"

#include <alsa/asoundlib.h>
#include <array>
#include <stdexcept>
#include <unistd.h>

namespace termify::platform {
namespace {

class LinuxFileSystem final : public posix::FileSystem {
public:
  std::filesystem::path ExecutableDirectory() const override {
    std::array<char, 4096> path{};
    const ssize_t count = readlink("/proc/self/exe", path.data(), path.size());
    if (count < 0 || static_cast<std::size_t>(count) == path.size())
      throw std::runtime_error("Failed to locate the Termify executable");
    return std::filesystem::path(
               std::string(path.data(), static_cast<std::size_t>(count)))
        .parent_path();
  }

protected:
  std::vector<std::filesystem::path>
  SystemExecutableCandidates(const std::string &name) const override {
    return {std::filesystem::path("/usr/local/share/termify/bin") / name};
  }
};

class AlsaAudioOutput final : public AudioOutput {
public:
  explicit AlsaAudioOutput(const AudioOutputConfig &config) {
    int result =
        snd_pcm_open(&_device, "pipewire", SND_PCM_STREAM_PLAYBACK, 0);
    if (result < 0)
      result = snd_pcm_open(&_device, "default", SND_PCM_STREAM_PLAYBACK, 0);
    if (result < 0)
      throw std::runtime_error("No ALSA playback device is available");
    if (snd_pcm_set_params(_device, SND_PCM_FORMAT_S16_LE,
                           SND_PCM_ACCESS_RW_INTERLEAVED, config.channels,
                           config.sampleRate, 1, 500000) < 0) {
      snd_pcm_close(_device);
      _device = nullptr;
      throw std::runtime_error("ALSA device configuration failed");
    }
  }

  ~AlsaAudioOutput() override {
    if (_device) {
      snd_pcm_drop(_device);
      snd_pcm_close(_device);
    }
  }

  void Reset() override {
    snd_pcm_drop(_device);
    snd_pcm_prepare(_device);
  }

  std::size_t WriteFrames(const std::int16_t *samples,
                          std::size_t frames) override {
    snd_pcm_sframes_t written = snd_pcm_writei(_device, samples, frames);
    if (written < 0) {
      if (snd_pcm_recover(_device, static_cast<int>(written), 1) < 0)
        return 0;
      written = snd_pcm_writei(_device, samples, frames);
    }
    return written > 0 ? static_cast<std::size_t>(written) : 0;
  }

private:
  snd_pcm_t *_device{nullptr};
};

class LinuxPlatform final : public PlatformServices {
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
    return std::make_unique<AlsaAudioOutput>(config);
  }

  void InstallInterruptHandler() override {
    posix::InstallInterruptHandler();
  }

private:
  posix::ProcessRunner _processes;
  LinuxFileSystem _files;
};

} // namespace

PlatformServices &CurrentPlatform() {
  static LinuxPlatform platform;
  return platform;
}

} // namespace termify::platform
