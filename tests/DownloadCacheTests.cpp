#include "Core/AudioDownloader.hpp"
#include <fstream>
#include <unistd.h>

using namespace termify::core;
namespace fs = std::filesystem;
#define CHECK(condition)                                                       \
  do {                                                                         \
    if (!(condition)) {                                                        \
      std::cerr << "Failed at " << __LINE__ << ": " #condition << '\n';        \
      return 1;                                                                \
    }                                                                          \
  } while (false)

int main(int argc, char **argv) {
  // Act as a deterministic downloader subprocess: no network or Python.
  if (argc > 1) {
    for (int i = 1; i < argc; ++i) {
      if (std::string(argv[i]) == "--skip-download") {
        std::cout << "https://www.youtube.com/watch?v=resolved\n";
        return 0;
      }
    }
    std::ofstream calls(std::getenv("TERMIFY_TEST_CALLS"), std::ios::app);
    calls << "download\n";
    for (int i = 1; i + 1 < argc; ++i) {
      if (std::string(argv[i]) != "-o")
        continue;
      std::string path = argv[i + 1];
      path.replace(path.find("%(ext)s"), 7, "mp3");
      std::ofstream output(path, std::ios::binary);
      output << "test audio payload";
      return 0;
    }
    return 1;
  }
  std::string pattern =
      (fs::temp_directory_path() / "termify-download-test-XXXXXX").string();
  CHECK(mkdtemp(pattern.data()));
  const fs::path root(pattern);
  struct Cleanup {
    fs::path path;
    ~Cleanup() {
      std::error_code e;
      fs::remove_all(path, e);
    }
  } cleanup{root};
  const auto callsFile = root / "calls";
  setenv("TERMIFY_TEST_CALLS", callsFile.c_str(), 1);
  setenv("TERMIFY_YTDLP", fs::absolute(argv[0]).c_str(), 1);
  auto count = [&] {
    std::ifstream input(callsFile);
    int calls = 0;
    std::string line;
    while (std::getline(input, line))
      ++calls;
    return calls;
  };
  auto &downloader = YtDlpManager::Self();
  CHECK(downloader.Init());
  bool missingRejected = false;
  try {
    downloader.PrepareAudio((root / "missing.wav").string());
  } catch (const std::runtime_error &) {
    missingRejected = true;
  }
  CHECK(missingRejected && count() == 0);
  std::string cachedPath;
  {
    LocalStore store(root / "data", root / "cache");
    const auto first = downloader.PrepareAudio("some song", nullptr, &store);
    CHECK(!first.temporary && first.cacheLease && count() == 1);
    cachedPath = first.path;
    const auto second = downloader.PrepareAudio("some song", nullptr, &store);
    CHECK(second.path == first.path && count() == 1);
    CHECK(second.source == "https://www.youtube.com/watch?v=resolved");
  }
  {
    LocalStore reopened(root / "data", root / "cache");
    auto cached = downloader.PrepareAudio("some song", nullptr, &reopened);
    CHECK(cached.path == cachedPath && count() == 1);
    cached.cacheLease.reset();
    fs::remove(cachedPath);
    auto refreshed = downloader.PrepareAudio("some song", nullptr, &reopened);
    CHECK(count() == 2 && fs::exists(refreshed.path));
    auto local = downloader.PrepareAudio(refreshed.path, nullptr, &reopened);
    CHECK(!local.temporary && local.path == refreshed.path && count() == 2);
  }
  {
    LocalStore full(root / "small-data", root / "small-cache", 0);
    auto temporary = downloader.PrepareAudio("uncached song", nullptr, &full);
    CHECK(temporary.temporary && !temporary.cacheLease &&
          fs::exists(temporary.path));
    fs::remove(temporary.path);
  }
  return 0;
}
