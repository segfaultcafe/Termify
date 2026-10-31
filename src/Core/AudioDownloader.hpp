#ifndef _TERMIFY_INCLUDE_AUDIO_DOWNLOADER_HPP
#define _TERMIFY_INCLUDE_AUDIO_DOWNLOADER_HPP

#include "Defines.hpp"
#include "LocalStore.hpp"

namespace termify::core {

struct PreparedAudio {
  string path;
  bool temporary{false};
  std::shared_ptr<CacheLease> cacheLease;
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

private:
  YtDlpManager() = default;

  std::filesystem::path getGlobalLocation();
  std::filesystem::path getYtDlpLocation();

private:
  std::filesystem::path _executablePath;
  std::filesystem::path _ytDlpExecutablePath;
  static bool _initialized;
};

} // namespace termify::core

#endif // _TERMIFY_INCLUDE_AUDIO_DOWNLOADER_HPP
