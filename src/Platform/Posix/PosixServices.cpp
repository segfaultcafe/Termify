#include "PosixServices.hpp"

#include <array>
#include <cerrno>
#include <csignal>
#include <cstdlib>
#include <fcntl.h>
#include <poll.h>
#include <stdexcept>
#include <string_view>
#include <thread>
#include <sys/file.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <termios.h>
#include <unistd.h>

namespace termify::platform::posix {
namespace {

constexpr std::string_view ENTER_SEQUENCE =
    "\033[?1049h\033[2J\033[H\033[?1000h\033[?1003h\033[?1006h\033[?25h";
constexpr std::string_view LEAVE_SEQUENCE =
    "\033[?1006l\033[?1003l\033[?1000l\033[?25h\033[?1049l";

class Lease final : public platform::FileLease {
public:
  explicit Lease(int descriptor) : _descriptor(descriptor) {}
  ~Lease() override { close(_descriptor); }

private:
  int _descriptor;
};

void writeAll(int descriptor, std::string_view value) {
  while (!value.empty()) {
    const ssize_t written = write(descriptor, value.data(), value.size());
    if (written > 0) {
      value.remove_prefix(static_cast<std::size_t>(written));
    } else if (written < 0 && errno == EINTR) {
      continue;
    } else {
      return;
    }
  }
}

int openRedirect(const std::filesystem::path &path) {
  return open(path.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0600);
}

void redirectDescriptor(const std::optional<std::filesystem::path> &path,
                        int target) {
  if (!path)
    return;
  const int descriptor = openRedirect(*path);
  if (descriptor < 0)
    _exit(126);
  if (dup2(descriptor, target) < 0)
    _exit(126);
  close(descriptor);
}

void terminateProcessGroup(pid_t child) {
  kill(-child, SIGTERM);
  int status = 0;
  while (waitpid(child, &status, 0) < 0 && errno == EINTR) {
  }
}

void handleInterrupt(int) {}

std::filesystem::path xdgDirectory(const char *variable, const char *fallback,
                                   const std::string &application) {
  if (const char *value = std::getenv(variable);
      value && *value && std::filesystem::path(value).is_absolute())
    return std::filesystem::path(value) / application;
  const char *home = std::getenv("HOME");
  if (!home || !*home || !std::filesystem::path(home).is_absolute())
    throw std::runtime_error(
        "HOME or an absolute XDG directory is required for storage");
  return std::filesystem::path(home) / fallback / application;
}

} // namespace

ProcessResult
ProcessRunner::Run(const ProcessRequest &request, const std::atomic_bool *cancel,
                   std::optional<std::chrono::milliseconds> timeout) {
  std::vector<std::string> values;
  values.reserve(request.arguments.size() + 1);
  values.push_back(request.executable.string());
  values.insert(values.end(), request.arguments.begin(),
                request.arguments.end());
  std::vector<char *> arguments;
  arguments.reserve(values.size() + 1);
  for (auto &value : values)
    arguments.push_back(value.data());
  arguments.push_back(nullptr);

  const pid_t child = fork();
  if (child < 0)
    throw std::runtime_error("Failed to start process");
  if (child == 0) {
    setpgid(0, 0);
    redirectDescriptor(request.standardOutput, STDOUT_FILENO);
    redirectDescriptor(request.standardError, STDERR_FILENO);
    if (request.discardStandardError) {
      const int nullDescriptor = open("/dev/null", O_WRONLY);
      if (nullDescriptor < 0 || dup2(nullDescriptor, STDERR_FILENO) < 0)
        _exit(126);
      close(nullDescriptor);
    }
    execv(request.executable.c_str(), arguments.data());
    _exit(127);
  }
  setpgid(child, child);

  const auto deadline = timeout ? std::optional(std::chrono::steady_clock::now() +
                                                 *timeout)
                                : std::nullopt;
  int status = 0;
  while (true) {
    const pid_t waited = waitpid(child, &status, WNOHANG);
    if (waited == child)
      break;
    if (waited < 0 && errno != EINTR)
      throw std::runtime_error("Failed while waiting for process");
    if (cancel && cancel->load()) {
      terminateProcessGroup(child);
      return {ProcessCompletion::Cancelled, -1};
    }
    if (deadline && std::chrono::steady_clock::now() >= *deadline) {
      terminateProcessGroup(child);
      return {ProcessCompletion::TimedOut, -1};
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
  }

  if (WIFEXITED(status))
    return {ProcessCompletion::Exited, WEXITSTATUS(status)};
  if (WIFSIGNALED(status))
    return {ProcessCompletion::Exited, 128 + WTERMSIG(status)};
  return {ProcessCompletion::Exited, -1};
}

std::optional<std::filesystem::path> FileSystem::FindExecutable(
    const std::string &name,
    const std::vector<std::filesystem::path> &preferred) const {
  std::vector<std::filesystem::path> candidates = preferred;
  const auto system = SystemExecutableCandidates(name);
  candidates.insert(candidates.end(), system.begin(), system.end());
  if (const char *pathValue = std::getenv("PATH")) {
    std::string paths(pathValue);
    std::size_t start = 0;
    while (start <= paths.size()) {
      const std::size_t end = paths.find(':', start);
      const std::string directory = paths.substr(start, end - start);
      if (!directory.empty())
        candidates.push_back(std::filesystem::path(directory) / name);
      if (end == std::string::npos)
        break;
      start = end + 1;
    }
  }
  for (const auto &candidate : candidates) {
    if (access(candidate.c_str(), X_OK) == 0)
      return candidate;
  }
  return std::nullopt;
}

std::filesystem::path FileSystem::TemporaryStem(const std::string &prefix) {
  static std::atomic_uint64_t sequence{0};
  return std::filesystem::temp_directory_path() /
         (prefix + "-" + std::to_string(getpid()) + "-" +
          std::to_string(sequence.fetch_add(1)));
}

std::filesystem::path
FileSystem::UserDataDirectory(const std::string &application) const {
  return xdgDirectory("XDG_DATA_HOME", ".local/share", application);
}

std::filesystem::path
FileSystem::UserCacheDirectory(const std::string &application) const {
  return xdgDirectory("XDG_CACHE_HOME", ".cache", application);
}

std::shared_ptr<FileLease>
FileSystem::TryLock(const std::filesystem::path &path,
                    FileLockMode mode) const {
  const int descriptor =
      open(path.c_str(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
  if (descriptor < 0)
    return {};
  const int operation = mode == FileLockMode::Shared ? LOCK_SH : LOCK_EX;
  if (flock(descriptor, operation | LOCK_NB) != 0) {
    close(descriptor);
    return {};
  }
  return std::make_shared<Lease>(descriptor);
}

LockedFile
FileSystem::CreateLockedFile(const std::filesystem::path &directory,
                             const std::string &prefix) const {
  std::string pattern = (directory / (prefix + "-XXXXXX")).string();
  const int descriptor = mkstemp(pattern.data());
  if (descriptor < 0)
    throw std::runtime_error("Could not create a locked temporary file");
  fcntl(descriptor, F_SETFD, FD_CLOEXEC);
  if (flock(descriptor, LOCK_SH) != 0) {
    close(descriptor);
    std::filesystem::remove(pattern);
    throw std::runtime_error("Could not lock a temporary file");
  }
  return {pattern, std::make_shared<Lease>(descriptor)};
}

struct TerminalDriver::State {
  State(int input, int output)
      : inputDescriptor(input), outputDescriptor(output),
        interactive(isatty(input) && isatty(output)) {}

  int inputDescriptor;
  int outputDescriptor;
  bool interactive{false};
  bool active{false};
  bool termiosActive{false};
  termios originalTermios{};
};

TerminalDriver::TerminalDriver(int inputDescriptor, int outputDescriptor)
    : _state(std::make_unique<State>(inputDescriptor, outputDescriptor)) {}

TerminalDriver::~TerminalDriver() { Leave(); }

bool TerminalDriver::Enter() {
  if (_state->active)
    return _state->interactive;
  if (!_state->interactive)
    return false;
  if (tcgetattr(_state->inputDescriptor, &_state->originalTermios) != 0) {
    _state->interactive = false;
    return false;
  }
  termios inputMode = _state->originalTermios;
  inputMode.c_lflag &= static_cast<tcflag_t>(~(ICANON | ECHO));
  inputMode.c_iflag &= static_cast<tcflag_t>(~(IXON | ICRNL));
  inputMode.c_cc[VMIN] = 1;
  inputMode.c_cc[VTIME] = 0;
  if (tcsetattr(_state->inputDescriptor, TCSAFLUSH, &inputMode) != 0) {
    _state->interactive = false;
    return false;
  }
  _state->termiosActive = true;
  writeAll(_state->outputDescriptor, ENTER_SEQUENCE);
  _state->active = true;
  return true;
}

void TerminalDriver::Leave() {
  if (!_state->active)
    return;
  writeAll(_state->outputDescriptor, LEAVE_SEQUENCE);
  if (_state->termiosActive) {
    tcsetattr(_state->inputDescriptor, TCSAFLUSH, &_state->originalTermios);
    _state->termiosActive = false;
  }
  _state->active = false;
}

bool TerminalDriver::Interactive() const { return _state->interactive; }

InputRead TerminalDriver::Read(std::size_t capacity) {
  InputRead result;
  result.bytes.resize(capacity);
  const ssize_t count =
      read(_state->inputDescriptor, result.bytes.data(), result.bytes.size());
  if (count > 0) {
    result.status = InputStatus::Data;
    result.bytes.resize(static_cast<std::size_t>(count));
  } else if (count == 0) {
    result.status = InputStatus::EndOfInput;
    result.bytes.clear();
  } else if (errno == EINTR) {
    result.status = InputStatus::Interrupted;
    result.bytes.clear();
  } else {
    result.status = InputStatus::Retry;
    result.bytes.clear();
  }
  return result;
}

bool TerminalDriver::WaitForInput(std::chrono::milliseconds timeout) const {
  pollfd descriptor{
      .fd = _state->inputDescriptor, .events = POLLIN, .revents = 0};
  int result = 0;
  do {
    result = poll(&descriptor, 1, static_cast<int>(timeout.count()));
  } while (result < 0 && errno == EINTR);
  return result > 0 && (descriptor.revents & POLLIN) != 0;
}

TerminalSize TerminalDriver::Size() const {
  winsize size{};
  if (ioctl(_state->outputDescriptor, TIOCGWINSZ, &size) == 0 &&
      size.ws_col > 0 && size.ws_row > 0)
    return {size.ws_col, size.ws_row};
  return {};
}

void InstallInterruptHandler() {
  struct sigaction action {};
  action.sa_handler = handleInterrupt;
  sigemptyset(&action.sa_mask);
  sigaction(SIGINT, &action, nullptr);
}

} // namespace termify::platform::posix
