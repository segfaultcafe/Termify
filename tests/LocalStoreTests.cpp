#include "Core/LocalStore.hpp"

#include <algorithm>
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
    CHECK(!store.SavedVolume());
    store.SaveVolume(45);
    CHECK(store.SavedVolume() == 45);
    store.SaveVolume(125);
    CHECK(store.SavedVolume() == 100);
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
    CHECK(store.SavedVolume() == 100);
    const auto history = store.History();
    CHECK(history.size() == 2);
    CHECK(history[0].source == "second" && history[1].title == "Été 🎵");
    CHECK(history[1].durationSeconds == 123 && history[1].playedAt == 42);
    CHECK(store.TrackTitle("second") == "Second");
    CHECK(!store.TrackTitle("missing"));
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
  CHECK(LocalStore::SourceHash("abc") ==
        "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
  const auto playlistData = root / "playlist-data";
  const auto playlistCache = root / "playlist-cache";
  {
    LocalStore store(playlistData, playlistCache, 1000);
    const auto initialPlaylists = store.Playlists();
    CHECK(initialPlaylists.size() == 1);
    const auto likedPlaylist = initialPlaylists.front();
    CHECK(likedPlaylist.name == "Liked Songs" && likedPlaylist.fixed &&
          likedPlaylist.trackCount == 0);
    CHECK(!store.RenamePlaylist(likedPlaylist.id, "Favorites"));
    CHECK(!store.DeletePlaylist(likedPlaylist.id));
    store.Record({0,
                  "search words",
                  "First title",
                  "First artist",
                  {},
                  180,
                  100,
                  false});
    auto first = store.StoreAudio("search words", audio,
                                  "https://www.youtube.com/watch?v=first");
    CHECK(first && first->source == "https://www.youtube.com/watch?v=first");
    store.Record({0,
                  "https://www.youtube.com/watch?v=second",
                  "Second title",
                  {},
                  {},
                  200,
                  101,
                  false});
    auto second =
        store.StoreAudio("https://www.youtube.com/watch?v=second", audio,
                         "https://www.youtube.com/watch?v=second");
    CHECK(second);
    const auto history = store.History();
    CHECK(history.size() == 2 && history[0].cached && history[1].cached);
    const std::string firstSource = "https://www.youtube.com/watch?v=first";
    CHECK(!store.IsLiked(firstSource));
    CHECK(store.Like({0,
                      firstSource,
                      "First title",
                      "First artist",
                      {},
                      180,
                      0,
                      false}) == AddTrackResult::Added);
    CHECK(store.IsLiked(firstSource));
    CHECK(store.Like({0,
                      firstSource,
                      "First title",
                      "First artist",
                      {},
                      180,
                      0,
                      false}) == AddTrackResult::Duplicate);
    const std::string secondSource =
        "https://www.youtube.com/watch?v=second";
    CHECK(store.Like({0, secondSource, "Second title", {}, {}, 200, 0,
                      false}) == AddTrackResult::Added);
    CHECK(store.Unlike(firstSource));
    CHECK(!store.IsLiked(firstSource));
    CHECK(!store.Unlike(firstSource));
    CHECK(store.IsLiked(secondSource));
    const auto remainingLiked = store.PlaylistTracks(likedPlaylist.id);
    CHECK(remainingLiked.size() == 1 &&
          remainingLiked[0].source == secondSource &&
          remainingLiked[0].position == 0);
    CHECK(store.Unlike(secondSource));
    CHECK(store.Like({0,
                      firstSource,
                      "First title",
                      "First artist",
                      {},
                      180,
                      0,
                      false}) == AddTrackResult::Added);

    const auto playlist = store.CreatePlaylist("Road trip");
    const auto searchPlaylist = store.CreatePlaylist("Search picks");
    const HistoryEntry searchResult{
        0, "https://www.youtube.com/watch?v=search-result", "Search title",
        "Search artist", {}, 240, 0, false};
    CHECK(store.AddVerifiedSourceToPlaylist(searchPlaylist, searchResult) ==
          AddTrackResult::Added);
    CHECK(store.AddVerifiedSourceToPlaylist(searchPlaylist, searchResult) ==
          AddTrackResult::Duplicate);
    const auto searchTracks = store.PlaylistTracks(searchPlaylist);
    CHECK(searchTracks.size() == 1 &&
          searchTracks[0].source == searchResult.source &&
          searchTracks[0].title == searchResult.title &&
          !searchTracks[0].cached);
    CHECK(store.TrackTitle(searchResult.source) == searchResult.title);
    CHECK(store.DeletePlaylist(searchPlaylist));
    CHECK(store.AddHistoryToPlaylist(playlist, history[1].id) ==
          AddTrackResult::Added);
    CHECK(store.AddHistoryToPlaylist(playlist, history[1].id) ==
          AddTrackResult::Duplicate);
    CHECK(store.AddHistoryToPlaylist(playlist, history[0].id) ==
          AddTrackResult::Added);
    auto tracks = store.PlaylistTracks(playlist);
    CHECK(tracks.size() == 2);
    CHECK(tracks[0].source == "https://www.youtube.com/watch?v=first");
    CHECK(tracks[0].sourceHash == LocalStore::SourceHash(tracks[0].source));
    CHECK(store.MovePlaylistTrack(playlist, tracks[1].id, -1));
    tracks = store.PlaylistTracks(playlist);
    CHECK(tracks[0].title == "Second title");
    CHECK(store.SetPlaylistShuffle(playlist, true));
    CHECK(store.SetPlaylistLoop(playlist, true));
    CHECK(store.RenamePlaylist(playlist, "Night drive"));
    const auto playlists = store.Playlists();
    CHECK(playlists.size() == 2 && playlists[0].fixed &&
          playlists[0].name == "Liked Songs" && playlists[0].trackCount == 1);
    const auto nightDrive =
        std::find_if(playlists.begin(), playlists.end(), [](const auto &item) {
          return item.name == "Night drive";
        });
    CHECK(nightDrive != playlists.end());
    CHECK(nightDrive->shuffle && nightDrive->loop &&
          nightDrive->trackCount == 2);
    CHECK(store.RemovePlaylistTrack(playlist, tracks[0].id));
    CHECK(store.PlaylistTracks(playlist).size() == 1);
  }
  {
    LocalStore reopened(playlistData, playlistCache, 1000);
    const auto playlists = reopened.Playlists();
    CHECK(playlists.size() == 2 && playlists[0].fixed &&
          playlists[0].trackCount == 1);
    CHECK(reopened.IsLiked("https://www.youtube.com/watch?v=first"));
    const auto nightDrive =
        std::find_if(playlists.begin(), playlists.end(), [](const auto &item) {
          return item.name == "Night drive";
        });
    CHECK(nightDrive != playlists.end() && nightDrive->trackCount == 1);
    const auto cached =
        reopened.FindAudio("https://www.youtube.com/watch?v=first");
    CHECK(cached && cached->source == "https://www.youtube.com/watch?v=first");
    CHECK(reopened.DeletePlaylist(nightDrive->id));
    CHECK(reopened.Playlists().size() == 1);
    CHECK(!reopened.DeletePlaylist(playlists[0].id));
    CHECK(reopened.FindAudio("https://www.youtube.com/watch?v=first"));
  }
  const auto migrationData = root / "migration-data";
  fs::create_directories(migrationData);
  sqlite3 *migrationDatabase = nullptr;
  CHECK(sqlite3_open((migrationData / "library.sqlite3").c_str(),
                     &migrationDatabase) == SQLITE_OK);
  CHECK(sqlite3_exec(
            migrationDatabase,
            "CREATE TABLE history (id INTEGER PRIMARY KEY,source TEXT NOT NULL,"
            "title TEXT NOT NULL,artist TEXT NOT NULL,album TEXT NOT NULL,"
            "duration INTEGER NOT NULL,played_at INTEGER NOT NULL);"
            "CREATE TABLE audio_cache (source TEXT PRIMARY KEY,filename TEXT "
            "UNIQUE NOT NULL,bytes INTEGER NOT NULL,touched INTEGER NOT NULL);"
            "PRAGMA user_version=1;",
            nullptr, nullptr, nullptr) == SQLITE_OK);
  sqlite3_close(migrationDatabase);
  {
    LocalStore migrated(migrationData, root / "migration-cache", 1000);
    const auto playlists = migrated.Playlists();
    CHECK(playlists.size() == 1 && playlists[0].fixed &&
          playlists[0].name == "Liked Songs");
    auto cached = migrated.StoreAudio(
        "old query", audio, "https://www.youtube.com/watch?v=migrated");
    CHECK(cached &&
          cached->source == "https://www.youtube.com/watch?v=migrated");
  }
  const auto unlimitedData = root / "unlimited-liked-data";
  {
    LocalStore store(unlimitedData, root / "unlimited-liked-cache", 1000);
    const auto playlists = store.Playlists();
    CHECK(playlists.size() == 1 && playlists[0].fixed);
    for (int i = 0; i <= LocalStore::PLAYLIST_TRACK_LIMIT; ++i) {
      const auto source = "https://example.com/liked/" + std::to_string(i);
      CHECK(store.AddVerifiedSourceToPlaylist(
                playlists[0].id,
                {0, source, "Liked track", {}, {}, 1, 0, false}) ==
            AddTrackResult::Added);
    }
    CHECK(store.PlaylistTracks(playlists[0].id).size() ==
          static_cast<std::size_t>(LocalStore::PLAYLIST_TRACK_LIMIT + 1));
  }
  // Refuse unknown schema versions rather than downgrading or deleting data.
  sqlite3 *database = nullptr;
  CHECK(sqlite3_open((data / "library.sqlite3").c_str(), &database) ==
        SQLITE_OK);
  CHECK(sqlite3_exec(database, "PRAGMA user_version=5", nullptr, nullptr,
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
