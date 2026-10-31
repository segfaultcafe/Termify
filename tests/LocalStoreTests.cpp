#include "Core/LocalStore.hpp"

#include <fstream>
#include <iostream>
#include <sqlite3.h>
#include <unistd.h>

using namespace termify::core;
namespace fs = std::filesystem;

#define CHECK(condition)                                                       \
  do {                                                                         \
    if (!(condition)) {                                                        \
      std::cerr << "Failed at line " << __LINE__ << ": " #condition << '\n';   \
      return 1;                                                                \
    }                                                                          \
  } while (false)

int main() {
  std::string pattern =
      (fs::temp_directory_path() / "termify-store-test-XXXXXX").string();
  const char *created = mkdtemp(pattern.data());
  CHECK(created);
  const fs::path root(created);
  struct Cleanup {
    fs::path path;
    ~Cleanup() {
      std::error_code e;
      fs::remove_all(path, e);
    }
  } cleanup{root};
  const auto data = root / "data", cache = root / "cache",
             audio = root / "source.mp3";
  {
    std::ofstream file(audio, std::ios::binary);
    file << std::string(40, 'a');
  }
  std::string firstPath;
  {
    LocalStore store(data, cache, 100);
    CHECK(store.History().empty());
    store.Record(
        {0, "source ' quoted", "Été 🎵", "Artist", "Album", 123, 42, false});
    store.QueueRecord({0, "second", "Second", {}, {}, 60, 43, false});
    auto first = store.StoreAudio("first", audio);
    CHECK(first);
    firstPath = first->path;
    auto hit = store.FindAudio("first");
    CHECK(hit && hit->path == firstPath);
    CHECK(fs::file_size(firstPath) == 40);
    auto second = store.StoreAudio("second", audio);
    CHECK(second);
    CHECK(!store.StoreAudio("third", audio)); // Both cached files are leased.
    second.reset();
    auto third = store.StoreAudio("third", audio);
    CHECK(third &&
          fs::exists(firstPath)); // Evict unleased second, not active first.
    CHECK(!store.FindAudio("second"));
    third.reset();
    first.reset();
    hit.reset();
    CHECK(store.FindAudio("first")); // Touch first: third is now least recent.
    auto fourth = store.StoreAudio("fourth", audio);
    CHECK(fourth && !store.FindAudio("third") && store.FindAudio("first"));
    {
      std::ofstream file(root / "large.mp3");
      file << std::string(101, 'x');
    }
    CHECK(!store.StoreAudio("oversize", root / "large.mp3"));
    CHECK(fs::exists(audio)); // Never delete or alter the user's source.
  } // Destructor flushes asynchronous history writes.
  {
    LocalStore store(data, cache, 100);
    const auto history = store.History();
    CHECK(history.size() == 2);
    CHECK(history[0].source == "second" && history[1].title == "Été 🎵");
    CHECK(history[1].durationSeconds == 123 && history[1].playedAt == 42);
    CHECK(store.FindAudio("first"));
    LocalStore other(data, cache, 100);
    auto lease = other.FindAudio("first");
    CHECK(lease);
    auto fifth = store.StoreAudio("fifth", audio);
    CHECK(fifth && fs::exists(firstPath)); // Lease in another store/connection.
    fifth.reset();
    lease.reset();
    fs::remove(firstPath);
    CHECK(!store.FindAudio("first")); // A manually cleared cache is a miss.
    CHECK(store.StoreAudio("first", audio)); // Stale index is repaired.
    for (int i = 0; i < LocalStore::HISTORY_LIMIT + 2; ++i)
      store.Record({0, std::to_string(i), "Track", {}, {}, 1, i + 100, false});
    CHECK(store.History().size() == LocalStore::HISTORY_LIMIT);
    CHECK(store.History().front().source == "1001");
  }
  {
    std::ofstream file(cache / "audio-orphan");
    file << "left by a crash";
  }
  {
    LocalStore store(data, cache, 100);
    CHECK(!fs::exists(cache / "audio-orphan"));
  }
  // Refuse unknown schema versions rather than downgrading or deleting data.
  sqlite3 *database = nullptr;
  CHECK(sqlite3_open((data / "library.sqlite3").c_str(), &database) ==
        SQLITE_OK);
  CHECK(sqlite3_exec(database, "PRAGMA user_version=2", nullptr, nullptr,
                     nullptr) == SQLITE_OK);
  sqlite3_close(database);
  bool rejected = false;
  try {
    LocalStore newer(data, cache);
  } catch (const std::exception &) {
    rejected = true;
  }
  CHECK(rejected);
  return 0;
}
