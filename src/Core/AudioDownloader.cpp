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
constexpr auto SEARCH_CACHE_TTL = std::chrono::minutes(30);
constexpr std::size_t SEARCH_CACHE_LIMIT = 20;

string cleanField(string value) {
  value.erase(std::remove_if(value.begin(), value.end(),
                             [](unsigned char character) {
                               return character < 32 || character == 127;
                             }),
              value.end());
  return value == "NA" ? string{} : value;
}

vector<string> splitSearchLine(const string &line) {
  vector<string> fields;
  std::size_t start = 0;
  while (start <= line.size()) {
    const auto end = line.find('\x1f', start);
    fields.push_back(line.substr(start, end - start));
    if (end == string::npos)
      break;
    start = end + 1;
  }
  return fields;
}

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
    return {.path = path,
            .source = path,
            .title = std::filesystem::path(path).stem().string(),
            .temporary = false,
            .cacheLease = {}};
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
        const auto storedTitle = cached->title.empty()
                                     ? store->TrackTitle(cached->source)
                                     : std::optional<string>(cached->title);
        return {cached->path, cached->source, storedTitle.value_or(""), false,
                std::move(cached->lease)};
      }
    } catch (const std::exception &) { /* Playback survives cache failures. */
    }
  }
  const auto downloaded = download(query, cancel);
  if (store) {
    try {
      if (auto cached =
              store->StoreAudio(query, downloaded.path, downloaded.source,
                                downloaded.title)) {
        std::error_code error;
        std::filesystem::remove(downloaded.path, error);
        return {cached->path, cached->source, downloaded.title, false,
                std::move(cached->lease)};
      }
    } catch (
        const std::exception &) { /* Keep the temporary download playable. */
    }
  }
  return {downloaded.path, downloaded.source, downloaded.title, true, {}};
}

string YtDlpManager::DownloadAudioToFile(const string &query,
                                         const atomic_bool *cancel) {
  return download(query, cancel).path;
}

string YtDlpManager::normalizeSearchQuery(const string &query) {
  string normalized;
  bool pendingSpace = false;
  for (const unsigned char character : query) {
    if (std::isspace(character)) {
      pendingSpace = !normalized.empty();
      continue;
    }
    if (pendingSpace) {
      normalized.push_back(' ');
      pendingSpace = false;
    }
    normalized.push_back(static_cast<char>(std::tolower(character)));
  }
  return normalized;
}

SearchResponse YtDlpManager::SearchYouTube(const string &query,
                                           std::size_t limit) {
  if (!_initialized)
    throw std::runtime_error(
        "yt-dlp is unavailable; reinstall Termify or set TERMIFY_YTDLP");
  const string normalized = normalizeSearchQuery(query);
  if (normalized.empty())
    throw std::runtime_error("Enter a song, artist, or album to search");
  limit = std::clamp<std::size_t>(limit, 1, 25);
  const string key = normalized + "\n" + std::to_string(limit);
  const auto now = std::chrono::steady_clock::now();
  {
    std::lock_guard lock(_searchCacheMtx);
    for (auto iterator = _searchCache.begin(); iterator != _searchCache.end();) {
      if (now - iterator->second.createdAt >= SEARCH_CACHE_TTL)
        iterator = _searchCache.erase(iterator);
      else
        ++iterator;
    }
    if (auto cached = _searchCache.find(key); cached != _searchCache.end()) {
      cached->second.lastUsed = now;
      return {cached->second.results, true};
    }
  }

  static std::atomic_uint64_t sequence{0};
  const string stem = "/tmp/termify-search-" + std::to_string(getpid()) + "-" +
                      std::to_string(sequence.fetch_add(1));
  const std::filesystem::path outputPath = stem + ".results";
  const std::filesystem::path errorPath = stem + ".log";
  const string source =
      "ytsearch" + std::to_string(limit) + ":" + normalized;
  const string outputFormat =
      "%(id)s\x1f%(title)s\x1f%(channel)s\x1f%(duration)s\x1f%(webpage_url)s";

  const pid_t child = fork();
  if (child < 0)
    throw std::runtime_error("Failed to start YouTube search");
  if (child == 0) {
    setpgid(0, 0);
    const int output =
        open(outputPath.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0600);
    if (output >= 0) {
      dup2(output, STDOUT_FILENO);
      close(output);
    }
    const int errorLog =
        open(errorPath.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0600);
    if (errorLog >= 0) {
      dup2(errorLog, STDERR_FILENO);
      close(errorLog);
    }
    execl(_ytDlpExecutablePath.c_str(), _ytDlpExecutablePath.c_str(),
          "--quiet", "--no-warnings", "--no-playlist", "--flat-playlist",
          "--skip-download", "--socket-timeout", "10", "--retries", "1",
          "--extractor-retries", "1", "--print", outputFormat.c_str(),
          source.c_str(), static_cast<char *>(nullptr));
    _exit(127);
  }
  setpgid(child, child);
  int status = 0;
  bool timedOut = false;
  const auto deadline = std::chrono::steady_clock::now() +
                        std::chrono::seconds(30);
  while (true) {
    const auto waited = waitpid(child, &status, WNOHANG);
    if (waited == child)
      break;
    if (waited < 0 && errno != EINTR)
      throw std::runtime_error("Failed while waiting for YouTube search");
    if (std::chrono::steady_clock::now() >= deadline) {
      timedOut = true;
      kill(-child, SIGTERM);
      waitpid(child, &status, 0);
      break;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
  }

  vector<SearchResult> results;
  if (WIFEXITED(status) && WEXITSTATUS(status) == 0) {
    std::ifstream output(outputPath);
    string line;
    while (results.size() < limit && std::getline(output, line)) {
      auto fields = splitSearchLine(line);
      if (fields.size() != 5)
        continue;
      for (auto &field : fields)
        field = cleanField(std::move(field));
      string canonical = fields[4];
      if (!canonical.starts_with("https://") &&
          !canonical.starts_with("http://")) {
        if (fields[0].empty())
          continue;
        canonical = "https://www.youtube.com/watch?v=" + fields[0];
      }
      std::int64_t duration = 0;
      try {
        if (!fields[3].empty())
          duration = std::max<std::int64_t>(0, std::stoll(fields[3]));
      } catch (const std::exception &) {
      }
      if (!fields[1].empty())
        results.push_back({fields[0], std::move(canonical), fields[1],
                           fields[2], duration});
    }
  }
  const string detail =
      results.empty()
          ? (timedOut ? "the provider did not respond within 30 seconds"
                      : readDownloadError(errorPath))
          : "";
  std::error_code error;
  std::filesystem::remove(outputPath, error);
  std::filesystem::remove(errorPath, error);
  if (results.empty())
    throw std::runtime_error("YouTube search failed: " + detail);

  {
    std::lock_guard lock(_searchCacheMtx);
    if (_searchCache.size() >= SEARCH_CACHE_LIMIT) {
      const auto oldest = std::min_element(
          _searchCache.begin(), _searchCache.end(), [](const auto &left,
                                                       const auto &right) {
            return left.second.lastUsed < right.second.lastUsed;
          });
      if (oldest != _searchCache.end())
        _searchCache.erase(oldest);
    }
    _searchCache[key] = {results, now, now};
  }
  return {std::move(results), false};
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

    const string metadataFormat =
        "after_move:%(webpage_url)s\x1f%(title)s";
    execl(_ytDlpExecutablePath.c_str(), _ytDlpExecutablePath.c_str(), "--quiet",
          "--no-warnings", "--no-playlist", "--extract-audio", "--audio-format",
          "mp3", "--embed-metadata", "--print", metadataFormat.c_str(),
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
  string title;
  {
    std::ifstream sourceStream(sourcePath);
    string metadata;
    std::getline(sourceStream, metadata);
    const auto separator = metadata.find('\x1f');
    canonicalSource = metadata.substr(0, separator);
    if (separator != string::npos)
      title = cleanField(metadata.substr(separator + 1));
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
  return {output, canonicalSource, std::move(title)};
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
