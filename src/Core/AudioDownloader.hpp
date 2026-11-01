#ifndef _TERMIFY_INCLUDE_AUDIO_DOWNLOADER_HPP
#define _TERMIFY_INCLUDE_AUDIO_DOWNLOADER_HPP

#include "Defines.hpp"
#include "LocalStore.hpp"

#include <chrono>

namespace termify::core {

struct PreparedAudio {
  string path;
  string source;
  string title;
  bool temporary{false};
  std::shared_ptr<CacheLease> cacheLease;
};

struct SearchResult {
  string id;
  string source;
  string title;
  string artist;
  std::int64_t durationSeconds{0};
};

struct SearchResponse {
  vector<SearchResult> results;
  bool fromCache{false};
};

class YtDlpManager {

public:
  static YtDlpManager &Self();
  bool Init();

  PreparedAudio PrepareAudio(const string &query,
                             const atomic_bool *cancel = nullptr,
                             LocalStore *store = nullptr);
  string DownloadAudioToFile(const string &query,
                             const atomic_bool *cancel = nullptr);
  SearchResponse SearchYouTube(const string &query, std::size_t limit = 10);

private:
  struct DownloadedAudio {
    string path;
    string source;
    string title;
  };
  DownloadedAudio download(const string &query,
                           const atomic_bool *cancel = nullptr);
  string resolveSource(const string &query,
                       const atomic_bool *cancel = nullptr);
  static string normalizeSearchQuery(const string &query);
  YtDlpManager() = default;

  struct SearchCacheEntry {
    vector<SearchResult> results;
    std::chrono::steady_clock::time_point createdAt;
    std::chrono::steady_clock::time_point lastUsed;
  };

  std::filesystem::path getGlobalLocation();
  std::filesystem::path getYtDlpLocation();

private:
  std::filesystem::path _executablePath;
  std::filesystem::path _ytDlpExecutablePath;
  std::mutex _searchCacheMtx;
  std::unordered_map<string, SearchCacheEntry> _searchCache;
  static bool _initialized;
};

} // namespace termify::core

#endif // _TERMIFY_INCLUDE_AUDIO_DOWNLOADER_HPP
