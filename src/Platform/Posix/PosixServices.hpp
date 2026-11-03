#ifndef TERMIFY_PLATFORM_POSIX_SERVICES_HPP
#define TERMIFY_PLATFORM_POSIX_SERVICES_HPP

#include "../Services.hpp"

namespace termify::platform::posix {

class ProcessRunner final : public platform::ProcessRunner {
public:
  ProcessResult
  Run(const ProcessRequest &request, const std::atomic_bool *cancel,
      std::optional<std::chrono::milliseconds> timeout) override;
};

class FileSystem : public platform::FileSystem {
public:
  std::optional<std::filesystem::path>
  FindExecutable(const std::string &name,
                 const std::vector<std::filesystem::path> &preferred) const
      override;
  std::filesystem::path TemporaryStem(const std::string &prefix) override;
  std::filesystem::path
  UserDataDirectory(const std::string &application) const override;
  std::filesystem::path
  UserCacheDirectory(const std::string &application) const override;
  std::shared_ptr<FileLease>
  TryLock(const std::filesystem::path &path, FileLockMode mode) const override;
  LockedFile CreateLockedFile(const std::filesystem::path &directory,
                              const std::string &prefix) const override;

protected:
  virtual std::vector<std::filesystem::path>
  SystemExecutableCandidates(const std::string &name) const = 0;
};

class TerminalDriver final : public platform::TerminalDriver {
public:
  TerminalDriver(int inputDescriptor, int outputDescriptor);
  ~TerminalDriver() override;

  bool Enter() override;
  void Leave() override;
  bool Interactive() const override;
  InputRead Read(std::size_t capacity) override;
  bool WaitForInput(std::chrono::milliseconds timeout) const override;
  TerminalSize Size() const override;

private:
  struct State;
  std::unique_ptr<State> _state;
};

void InstallInterruptHandler();

} // namespace termify::platform::posix

#endif // TERMIFY_PLATFORM_POSIX_SERVICES_HPP
