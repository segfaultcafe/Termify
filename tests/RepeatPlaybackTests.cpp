#include "Core/Mixer.hpp"
#include "Platform/Services.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <thread>

using namespace std::chrono_literals;
using namespace termify::core;

#define CHECK(condition)                                                       \
  do {                                                                         \
    if (!(condition)) {                                                        \
      std::cerr << "Check failed at line " << __LINE__ << ": " #condition      \
                << '\n';                                                       \
      return 1;                                                                \
    }                                                                          \
  } while (false)

namespace {

std::atomic<int> outputCreations{0};
std::atomic<int> outputDrains{0};

class TestAudioOutput final : public termify::platform::AudioOutput {
public:
  TestAudioOutput() { ++outputCreations; }
  void Reset() override {}
  std::size_t WriteFrames(const std::int16_t *, std::size_t frames) override {
    return frames;
  }
  void Drain(const std::atomic_bool *) override { ++outputDrains; }
};

class TestProcessRunner final : public termify::platform::ProcessRunner {
public:
  termify::platform::ProcessResult
  Run(const termify::platform::ProcessRequest &, const std::atomic_bool *,
      std::optional<std::chrono::milliseconds>) override {
    return {};
  }
};

class TestFileSystem final : public termify::platform::FileSystem {
public:
  std::filesystem::path ExecutableDirectory() const override { return {}; }
  std::filesystem::path TemporaryDirectory() const override {
    return std::filesystem::temp_directory_path();
  }
  std::optional<std::filesystem::path>
  FindExecutable(const std::string &,
                 const std::vector<std::filesystem::path> &) const override {
    return std::nullopt;
  }
  std::filesystem::path TemporaryStem(const std::string &) override {
    return std::filesystem::temp_directory_path() / "termify-repeat-test";
  }
  std::filesystem::path UserDataDirectory(const std::string &) const override {
    return std::filesystem::temp_directory_path();
  }
  std::filesystem::path UserCacheDirectory(const std::string &) const override {
    return std::filesystem::temp_directory_path();
  }
  std::shared_ptr<termify::platform::FileLease>
  TryLock(const std::filesystem::path &,
          termify::platform::FileLockMode) const override {
    return {};
  }
  termify::platform::LockedFile
  CreateLockedFile(const std::filesystem::path &,
                   const std::string &) const override {
    return {};
  }
};

class TestTerminal final : public termify::platform::TerminalDriver {
public:
  bool Enter() override { return true; }
  void Leave() override {}
  bool Interactive() const override { return false; }
  termify::platform::InputRead Read(std::size_t) override { return {}; }
  bool WaitForInput(std::chrono::milliseconds) const override { return false; }
  termify::platform::TerminalSize Size() const override { return {}; }
};

class TestPlatform final : public termify::platform::PlatformServices {
public:
  termify::platform::ProcessRunner &Processes() override { return _processes; }
  termify::platform::FileSystem &Files() override { return _files; }
  std::unique_ptr<termify::platform::TerminalDriver>
  CreateTerminal(int, int) override {
    return std::make_unique<TestTerminal>();
  }
  std::unique_ptr<termify::platform::AudioOutput>
  CreateAudioOutput(const termify::platform::AudioOutputConfig &) override {
    return std::make_unique<TestAudioOutput>();
  }
  void InstallInterruptHandler() override {}

private:
  TestProcessRunner _processes;
  TestFileSystem _files;
};

void writeWave(const std::filesystem::path &path) {
  constexpr std::uint32_t sampleRate = 44100;
  constexpr std::uint16_t channels = 1;
  constexpr std::uint16_t bits = 16;
  constexpr std::uint32_t frames = 441;
  constexpr std::uint32_t dataBytes = frames * channels * bits / 8;
  constexpr std::uint32_t riffBytes = 36 + dataBytes;
  constexpr std::uint32_t byteRate = sampleRate * channels * bits / 8;
  constexpr std::uint16_t blockAlign = channels * bits / 8;
  std::ofstream output(path, std::ios::binary);
  const auto write = [&output](const auto &value) {
    output.write(reinterpret_cast<const char *>(&value), sizeof(value));
  };
  output.write("RIFF", 4);
  write(riffBytes);
  output.write("WAVEfmt ", 8);
  constexpr std::uint32_t formatBytes = 16;
  constexpr std::uint16_t pcm = 1;
  write(formatBytes);
  write(pcm);
  write(channels);
  write(sampleRate);
  write(byteRate);
  write(blockAlign);
  write(bits);
  output.write("data", 4);
  write(dataBytes);
  std::int16_t silence = 0;
  for (std::uint32_t frame = 0; frame < frames; ++frame)
    write(silence);
}

} // namespace

namespace termify::platform {
PlatformServices &CurrentPlatform() {
  static TestPlatform platform;
  return platform;
}
} // namespace termify::platform

int main() {
  const auto source =
      std::filesystem::temp_directory_path() / "termify-repeat-playback.wav";
  writeWave(source);

  PlaybackContext playback;
  RingBufferContext ringBuffer;
  VisualizationContext visualization;
  MixerResponseContext responses;
  SongQueue queue;
  ringBuffer.ringBuffer.resize(ringBuffer.CHANNELS * ringBuffer.BUFFER_FRAMES);
  playback.gRunning.store(true);

  {
    Mixer mixer(&playback, &ringBuffer, &visualization, &responses, &queue);
    mixer.ToggleRepeat(true);
    CHECK(mixer.AddToQueue(source.string()));

    const auto deadline = std::chrono::steady_clock::now() + 5s;
    while (outputDrains.load() < 500 &&
           std::chrono::steady_clock::now() < deadline)
      std::this_thread::sleep_for(10ms);

    if (outputDrains.load() < 500) {
      std::cerr << "Repeat stalled: drains=" << outputDrains.load()
                << " outputs=" << outputCreations.load()
                << " playing=" << playback.isPlaying.load()
                << " decode-finished=" << ringBuffer.decodeFinished.load()
                << " read=" << ringBuffer.readIndex.load()
                << " write=" << ringBuffer.writeIndex.load() << '\n';
    }
    CHECK(outputDrains.load() >= 500);
    CHECK(outputCreations.load() == 1);
    mixer.ToggleRepeat(false);
    const auto stopped = std::chrono::steady_clock::now() + 2s;
    while (playback.isPlaying.load() &&
           std::chrono::steady_clock::now() < stopped)
      std::this_thread::sleep_for(10ms);
    if (playback.isPlaying.load()) {
      std::cerr << "Repeat did not stop: drains=" << outputDrains.load()
                << " outputs=" << outputCreations.load()
                << " decode-finished=" << ringBuffer.decodeFinished.load()
                << " read=" << ringBuffer.readIndex.load()
                << " write=" << ringBuffer.writeIndex.load() << '\n';
    }
    CHECK(!playback.isPlaying.load());
    mixer.Shutdown();
  }

  std::error_code error;
  std::filesystem::remove(source, error);
  return 0;
}
