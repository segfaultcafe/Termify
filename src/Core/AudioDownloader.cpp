#include "AudioDownloader.hpp"

#include <cctype>
#include <cerrno>
#include <fcntl.h>
#include <fstream>
#include <signal.h>
#include <stdexcept>
#include <sys/wait.h>
#include <unistd.h>

namespace termify::core {

namespace {
string readDownloadError(const std::filesystem::path &path) {
  std::ifstream stream(path);
  string line;
  string lastLine;
  string errorLine;
  while (std::getline(stream, line)) {
    if (!line.empty()) {
      lastLine = line;
    }
    if (line.find("ERROR:") != string::npos) {
      errorLine = line;
    }
  }

  string message = errorLine.empty() ? lastLine : errorLine;
  if (const auto marker = message.find("ERROR:"); marker != string::npos) {
    message.erase(0, marker + 6);
  }
  message.erase(message.begin(), std::find_if(message.begin(), message.end(),
                                              [](unsigned char character) {
                                                return !std::isspace(character);
                                              }));
  message.erase(std::remove_if(message.begin(), message.end(),
                               [](unsigned char character) {
                                 return character < 32 && character != '\t';
                               }),
                message.end());
  if (message.find("Failed to resolve") != string::npos ||
      message.find("name resolution") != string::npos) {
    return "Network error: YouTube's address could not be resolved.";
  }
  if (message.find("Sign in to confirm") != string::npos) {
    return "YouTube requires sign-in; yt-dlp cookies must be configured.";
  }
  if (message.find("HTTP Error 403") != string::npos) {
    return "YouTube rejected the request (HTTP 403); update yt-dlp or use "
           "cookies.";
  }
  if (message.size() > 220) {
    message.resize(217);
    message += "...";
  }
  return message.empty() ? "yt-dlp exited without an error message" : message;
}
} // namespace

bool YtDlpManager::_initialized = false;

YtDlpManager &YtDlpManager::Self() {
  static YtDlpManager instance;
  return instance;
}

bool YtDlpManager::Init() {
  if (_initialized)
    return true;

  try {
    _executablePath = getGlobalLocation();
    _ytDlpExecutablePath = getYtDlpLocation();
    if (access(_ytDlpExecutablePath.c_str(), X_OK) != 0) {
      return false;
    }
  } catch (const std::exception &e) {
    return false;
  }

  _initialized = true;
  return true;
}

PreparedAudio YtDlpManager::PrepareAudio(const string &query,
                                         const atomic_bool *cancel,
                                         LocalStore *store) {
  std::error_code pathError;
  if (std::filesystem::is_regular_file(query, pathError)) {
    const auto path = std::filesystem::absolute(query).string();
    return {.path = path, .source = path, .temporary = false, .cacheLease = {}};
  }
  if (std::filesystem::path(query).is_absolute())
    throw std::runtime_error("Local audio file is missing or unreadable: " +
                             query);
  if (store) {
    try {
      if (auto cached = store->FindAudio(query)) {
        if (!cached->source.starts_with("https://") &&
            !cached->source.starts_with("http://")) {
          try {
            const auto canonical = resolveSource(query, cancel);
            if (store->UpdateCachedSource(query, canonical))
              cached->source = canonical;
          } catch (const std::exception &) {
            // Legacy cache hits stay playable while offline, but remain
            // ineligible for playlists until their URL can be resolved.
          }
        }
        return {cached->path, cached->source, false, std::move(cached->lease)};
      }
    } catch (const std::exception &) { /* Playback survives cache failures. */
    }
  }
  const auto downloaded = download(query, cancel);
  if (store) {
    try {
      if (auto cached =
              store->StoreAudio(query, downloaded.path, downloaded.source)) {
        std::error_code error;
        std::filesystem::remove(downloaded.path, error);
        return {cached->path, cached->source, false, std::move(cached->lease)};
      }
    } catch (
        const std::exception &) { /* Keep the temporary download playable. */
    }
  }
  return {downloaded.path, downloaded.source, true, {}};
}

string YtDlpManager::DownloadAudioToFile(const string &query,
                                         const atomic_bool *cancel) {
  return download(query, cancel).path;
}

YtDlpManager::DownloadedAudio
YtDlpManager::download(const string &query, const atomic_bool *cancel) {
  if (!_initialized)
    throw std::runtime_error(
        "yt-dlp is unavailable; reinstall Termify or set TERMIFY_YTDLP");

  static std::atomic_uint64_t sequence{0};
  const string stem = "/tmp/termify-audio-" + std::to_string(getpid()) + "-" +
                      std::to_string(sequence.fetch_add(1));
  const string outputTemplate = stem + ".%(ext)s";
  const std::filesystem::path errorPath = stem + ".log";
  const std::filesystem::path sourcePath = stem + ".source";
  const string source =
      query.starts_with("http://") || query.starts_with("https://")
          ? query
          : "ytsearch1:" + query;

  const pid_t child = fork();
  if (child < 0)
    throw std::runtime_error("Failed to start yt-dlp");

  if (child == 0) {
    setpgid(0, 0);
    const int sourceOutput =
        open(sourcePath.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0600);
    if (sourceOutput >= 0) {
      dup2(sourceOutput, STDOUT_FILENO);
      close(sourceOutput);
    }
    const int errorLog =
        open(errorPath.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0600);
    if (errorLog >= 0) {
      dup2(errorLog, STDERR_FILENO);
      close(errorLog);
    }

    execl(_ytDlpExecutablePath.c_str(), _ytDlpExecutablePath.c_str(), "--quiet",
          "--no-warnings", "--no-playlist", "--extract-audio", "--audio-format",
          "mp3", "--embed-metadata", "--print", "after_move:%(webpage_url)s",
          "-o", outputTemplate.c_str(), source.c_str(),
          static_cast<char *>(nullptr));
    _exit(127);
  }
  setpgid(child, child);

  int status = 0;
  while (true) {
    const pid_t result = waitpid(child, &status, WNOHANG);
    if (result == child)
      break;
    if (result < 0 && errno != EINTR)
      throw std::runtime_error("Failed while waiting for yt-dlp");
    if (cancel && cancel->load()) {
      kill(-child, SIGTERM);
      waitpid(child, &status, 0);
      std::error_code error;
      std::filesystem::remove(stem + ".mp3", error);
      std::filesystem::remove(stem + ".mp3.part", error);
      std::filesystem::remove(errorPath, error);
      std::filesystem::remove(sourcePath, error);
      throw std::runtime_error("Download cancelled");
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
  }

  const string output = stem + ".mp3";
  if (!WIFEXITED(status) || WEXITSTATUS(status) != 0 ||
      !std::filesystem::exists(output)) {
    const string detail = readDownloadError(errorPath);
    std::error_code error;
    std::filesystem::remove(output, error);
    std::filesystem::remove(output + ".part", error);
    std::filesystem::remove(errorPath, error);
    std::filesystem::remove(sourcePath, error);
    throw std::runtime_error("yt-dlp: " + detail);
  }
  std::error_code error;
  std::filesystem::remove(errorPath, error);
  string canonicalSource;
  {
    std::ifstream sourceStream(sourcePath);
    std::getline(sourceStream, canonicalSource);
  }
  std::filesystem::remove(sourcePath, error);
  canonicalSource.erase(
      std::remove_if(canonicalSource.begin(), canonicalSource.end(),
                     [](unsigned char character) {
                       return character < 32 || character == 127;
                     }),
      canonicalSource.end());
  if (!canonicalSource.starts_with("https://") &&
      !canonicalSource.starts_with("http://"))
    canonicalSource = query;
  return {output, canonicalSource};
}

string YtDlpManager::resolveSource(const string &query,
                                   const atomic_bool *cancel) {
  if (!_initialized)
    throw std::runtime_error("yt-dlp is unavailable");
  static std::atomic_uint64_t sequence{0};
  const string path = "/tmp/termify-source-" + std::to_string(getpid()) + "-" +
                      std::to_string(sequence.fetch_add(1));
  const string source =
      query.starts_with("http://") || query.starts_with("https://")
          ? query
          : "ytsearch1:" + query;
  const pid_t child = fork();
  if (child < 0)
    throw std::runtime_error("Failed to start yt-dlp");
  if (child == 0) {
    setpgid(0, 0);
    const int output = open(path.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0600);
    if (output >= 0) {
      dup2(output, STDOUT_FILENO);
      close(output);
    }
    const int devNull = open("/dev/null", O_WRONLY);
    if (devNull >= 0) {
      dup2(devNull, STDERR_FILENO);
      close(devNull);
    }
    execl(_ytDlpExecutablePath.c_str(), _ytDlpExecutablePath.c_str(), "--quiet",
          "--no-warnings", "--no-playlist", "--skip-download", "--print",
          "%(webpage_url)s", source.c_str(), static_cast<char *>(nullptr));
    _exit(127);
  }
  setpgid(child, child);
  int status = 0;
  while (true) {
    const pid_t result = waitpid(child, &status, WNOHANG);
    if (result == child)
      break;
    if (result < 0 && errno != EINTR)
      throw std::runtime_error("Failed while resolving media URL");
    if (cancel && cancel->load()) {
      kill(-child, SIGTERM);
      waitpid(child, &status, 0);
      std::error_code error;
      std::filesystem::remove(path, error);
      throw std::runtime_error("Source resolution cancelled");
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
  }
  string canonical;
  {
    std::ifstream stream(path);
    std::getline(stream, canonical);
  }
  std::error_code error;
  std::filesystem::remove(path, error);
  canonical.erase(
      std::remove_if(canonical.begin(), canonical.end(),
                     [](unsigned char ch) { return ch < 32 || ch == 127; }),
      canonical.end());
  if (!WIFEXITED(status) || WEXITSTATUS(status) != 0 ||
      (!canonical.starts_with("https://") && !canonical.starts_with("http://")))
    throw std::runtime_error("yt-dlp did not resolve a canonical media URL");
  return canonical;
}

std::filesystem::path YtDlpManager::getGlobalLocation() {
#if defined(_WIN32)
  char path[MAX_PATH];
  GetModuleFileNameA(nullptr, path, MAX_PATH);
  return std::filesystem::path(path).parent_path();

#elif defined(__linux__)
  char path[4096];
  ssize_t count = readlink("/proc/self/exe", path, sizeof(path));
  if (count == -1)
    throw std::runtime_error("Failed to read /proc/self/exe");
  return std::filesystem::path(string(path, count)).parent_path();

#elif defined(__APPLE__)
  char path[4096];
  uint32_t size = sizeof(path);
  if (_NSGetExecutablePath(path, &size) != 0)
    throw std::runtime_error("Buffer too small for executable path");
  return std::filesystem::path(path).parent_path();

#else
  static_assert(false, "Unsupported platform");
#endif
}

std::filesystem::path YtDlpManager::getYtDlpLocation() {
  if (const char *overridePath = std::getenv("TERMIFY_YTDLP")) {
    if (*overridePath != '\0' && access(overridePath, X_OK) == 0) {
      return overridePath;
    }
  }

  const vector<std::filesystem::path> bundledCandidates = {
      _executablePath / "yt-dlp",
      _executablePath.parent_path() / "lib/termify/yt-dlp",
#ifndef NDEBUG
      _executablePath / ".local/bin/yt-dlp",
#else
      "/usr/local/share/termify/bin/yt-dlp",
#endif
  };
  for (const auto &candidate : bundledCandidates) {
    if (access(candidate.c_str(), X_OK) == 0) {
      return candidate;
    }
  }

  if (const char *pathValue = std::getenv("PATH")) {
    const string paths(pathValue);
    std::size_t start = 0;
    while (start <= paths.size()) {
      const std::size_t end = paths.find(':', start);
      const string directory = paths.substr(start, end - start);
      if (!directory.empty()) {
        const auto candidate = std::filesystem::path(directory) / "yt-dlp";
        if (access(candidate.c_str(), X_OK) == 0) {
          return candidate;
        }
      }
      if (end == string::npos)
        break;
      start = end + 1;
    }
  }

  return bundledCandidates.front();
}

} // namespace termify::core
