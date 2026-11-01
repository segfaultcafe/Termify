#ifndef TERMIFY_PLATFORM_SERVICES_HPP
#define TERMIFY_PLATFORM_SERVICES_HPP

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace termify::platform {

enum class ProcessCompletion { Exited, Cancelled, TimedOut };

struct ProcessRequest {
  std::filesystem::path executable;
  std::vector<std::string> arguments;
  std::optional<std::filesystem::path> standardOutput;
  std::optional<std::filesystem::path> standardError;
  bool discardStandardError{false};
};

struct ProcessResult {
  ProcessCompletion completion{ProcessCompletion::Exited};
  int exitCode{-1};

  bool Succeeded() const {
    return completion == ProcessCompletion::Exited && exitCode == 0;
  }
};

class ProcessRunner {
public:
  virtual ~ProcessRunner() = default;
  virtual ProcessResult
  Run(const ProcessRequest &request,
      const std::atomic_bool *cancel = nullptr,
      std::optional<std::chrono::milliseconds> timeout = std::nullopt) = 0;
};

enum class FileLockMode { Shared, Exclusive };

class FileLease {
public:
  virtual ~FileLease() = default;
};

struct LockedFile {
  std::filesystem::path path;
  std::shared_ptr<FileLease> lease;
};

class FileSystem {
public:
  virtual ~FileSystem() = default;
  virtual std::filesystem::path ExecutableDirectory() const = 0;
  virtual std::optional<std::filesystem::path>
  FindExecutable(const std::string &name,
                 const std::vector<std::filesystem::path> &preferred) const = 0;
  virtual std::filesystem::path TemporaryStem(const std::string &prefix) = 0;
  virtual std::filesystem::path
  UserDataDirectory(const std::string &application) const = 0;
  virtual std::filesystem::path
  UserCacheDirectory(const std::string &application) const = 0;
  virtual std::shared_ptr<FileLease>
  TryLock(const std::filesystem::path &path, FileLockMode mode) const = 0;
  virtual LockedFile CreateLockedFile(const std::filesystem::path &directory,
                                      const std::string &prefix) const = 0;
};

struct TerminalSize {
  int columns{80};
  int rows{24};
};

enum class InputStatus { Data, EndOfInput, Interrupted, Retry };

struct InputRead {
  InputStatus status{InputStatus::Retry};
  std::vector<unsigned char> bytes;
};

class TerminalDriver {
public:
  virtual ~TerminalDriver() = default;
  virtual bool Enter() = 0;
  virtual void Leave() = 0;
  virtual bool Interactive() const = 0;
  virtual InputRead Read(std::size_t capacity) = 0;
  virtual bool WaitForInput(std::chrono::milliseconds timeout) const = 0;
  virtual TerminalSize Size() const = 0;
};

struct AudioOutputConfig {
  int sampleRate{44100};
  int channels{1};
};

class AudioOutput {
public:
  virtual ~AudioOutput() = default;
  virtual void Reset() = 0;
  virtual std::size_t WriteFrames(const std::int16_t *samples,
                                  std::size_t frames) = 0;
};

class PlatformServices {
public:
  virtual ~PlatformServices() = default;
  virtual ProcessRunner &Processes() = 0;
  virtual FileSystem &Files() = 0;
  virtual std::unique_ptr<TerminalDriver>
  CreateTerminal(int inputDescriptor, int outputDescriptor) = 0;
  virtual std::unique_ptr<AudioOutput>
  CreateAudioOutput(const AudioOutputConfig &config) = 0;
  virtual void InstallInterruptHandler() = 0;
};

// Exactly one OS implementation provides this composition root. Platform
// selection belongs to the build, never to application or core code.
PlatformServices &CurrentPlatform();

} // namespace termify::platform

#endif // TERMIFY_PLATFORM_SERVICES_HPP
