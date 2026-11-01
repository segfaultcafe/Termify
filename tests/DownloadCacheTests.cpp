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
    bool search = false;
    for (int i = 1; i < argc; ++i) {
      if (std::string(argv[i]).starts_with("ytsearch10:"))
        search = true;
    }
    if (search) {
      std::ofstream calls(std::getenv("TERMIFY_TEST_CALLS"), std::ios::app);
      calls << "search\n";
      std::cout
          << "first-id\x1f"
             "First result\x1f"
             "First artist\x1f"
             "125\x1f"
             "https://www.youtube.com/watch?v=first-id\n"
          << "second-id\x1f"
             "Second result\x1f"
             "Second artist\x1f"
             "240\x1f"
             "https://www.youtube.com/watch?v=second-id\n";
      return 0;
    }
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
      std::cout << "https://www.youtube.com/watch?v=resolved\x1f"
                   "Resolved title\n";
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
  auto count = [&](const std::string &kind) {
    std::ifstream input(callsFile);
    int calls = 0;
    std::string line;
    while (std::getline(input, line))
      if (line == kind)
        ++calls;
    return calls;
  };
  auto &downloader = YtDlpManager::Self();
  CHECK(downloader.Init());
  const auto search = downloader.SearchYouTube("  Daft   Punk  ");
  CHECK(!search.fromCache && search.results.size() == 2);
  CHECK(search.results[0].title == "First result");
  CHECK(search.results[0].durationSeconds == 125);
  const auto cachedSearch = downloader.SearchYouTube("daft punk");
  CHECK(cachedSearch.fromCache && cachedSearch.results.size() == 2);
  CHECK(count("search") == 1);
  bool missingRejected = false;
  try {
    downloader.PrepareAudio((root / "missing.wav").string());
  } catch (const std::runtime_error &) {
    missingRejected = true;
  }
  CHECK(missingRejected && count("download") == 0);
  std::string cachedPath;
  {
    LocalStore store(root / "data", root / "cache");
    const auto first = downloader.PrepareAudio("some song", nullptr, &store);
    CHECK(!first.temporary && first.cacheLease && count("download") == 1);
    CHECK(first.title == "Resolved title");
    cachedPath = first.path;
    const auto second = downloader.PrepareAudio("some song", nullptr, &store);
    CHECK(second.path == first.path && count("download") == 1);
    CHECK(second.source == "https://www.youtube.com/watch?v=resolved");
    CHECK(second.title == "Resolved title");
    store.Record({0, second.source, "Stored title", {}, {}, 1, 1, false});
    const auto titled = downloader.PrepareAudio(second.source, nullptr, &store);
    CHECK(titled.title == "Resolved title");
  }
  {
    LocalStore reopened(root / "data", root / "cache");
    auto cached = downloader.PrepareAudio("some song", nullptr, &reopened);
    CHECK(cached.path == cachedPath && count("download") == 1);
    cached.cacheLease.reset();
    fs::remove(cachedPath);
    auto refreshed = downloader.PrepareAudio("some song", nullptr, &reopened);
    CHECK(count("download") == 2 && fs::exists(refreshed.path));
    auto local = downloader.PrepareAudio(refreshed.path, nullptr, &reopened);
    CHECK(!local.temporary && local.path == refreshed.path &&
          count("download") == 2);
    CHECK(local.title == fs::path(refreshed.path).stem().string());
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
