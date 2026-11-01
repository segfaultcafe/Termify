#include "Core/Cli.hpp"
#include "Core/Mixer.hpp"

#include <filesystem>
#include <iostream>

using namespace termify::core;

#define CHECK(condition)                                                       \
  do {                                                                         \
    if (!(condition)) {                                                        \
      std::cerr << "Check failed at line " << __LINE__ << ": " #condition      \
                << '\n';                                                       \
      return 1;                                                                \
    }                                                                          \
  } while (false)

struct MixerRig {
  PlaybackContext playback;
  RingBufferContext ringBuffer;
  VisualizationContext visualization;
  MixerResponseContext responses;
  SongQueue queue;
  std::unique_ptr<Mixer> mixer;

  explicit MixerRig(bool running = false, LocalStore *store = nullptr) {
    playback.gRunning.store(running);
    mixer = std::make_unique<Mixer>(&playback, &ringBuffer, &visualization,
                                    &responses, &queue, store);
  }
};

int main() {
  const auto absolute90 = std::optional<SeekArgument>{{90, false}};
  const auto absolute3723 = std::optional<SeekArgument>{{3723, false}};
  const auto relative15 = std::optional<SeekArgument>{{15, true}};
  const auto relativeMinus30 = std::optional<SeekArgument>{{-30, true}};
  CHECK(ParseSeekArgument("90") == absolute90);
  CHECK(ParseSeekArgument("1:30") == absolute90);
  CHECK(ParseSeekArgument("1:02:03") == absolute3723);
  CHECK(ParseSeekArgument("+15") == relative15);
  CHECK(ParseSeekArgument("-0:30") == relativeMinus30);
  CHECK(!ParseSeekArgument("1:60"));
  CHECK(!ParseSeekArgument("1:2:3:4"));
  CHECK(!ParseSeekArgument("nope"));

  {
    MixerRig rig;
    CHECK(rig.mixer->StartPlaylist({{"song-a"}, {"song-b"}}, "Test", false,
                                   false));
    const auto current = rig.queue.PopForPlayback();
    CHECK(current && current->query == "song-a");

    CHECK(rig.mixer->AddToQueue("darude sandstorm"));
    auto upcoming = rig.queue.Snapshot();
    CHECK(upcoming.size() == 2);
    CHECK(upcoming[0].query == "darude sandstorm");
    CHECK(upcoming[0].origin == QueueOrigin::Manual);
    CHECK(upcoming[1].query == "song-b");
    CHECK(upcoming[1].origin == QueueOrigin::Playlist);

    CHECK(rig.mixer->AddToQueue("song-b"));
    upcoming = rig.queue.Snapshot();
    CHECK(upcoming.size() == 2);
    CHECK(upcoming[0].query == "song-b");
    CHECK(upcoming[1].query == "darude sandstorm");
    CHECK(rig.mixer->AddToQueue("song-b"));
    CHECK(rig.queue.Size() == 2);
  }

  {
    MixerRig rig;
    CHECK(
        rig.mixer->StartPlaylist({{"song-a"}, {"song-b"}}, "Looping", false,
                                 true));
    CHECK(rig.queue.PopForPlayback()->query == "song-a");
    CHECK(rig.mixer->IsPlaylistLooping());
    auto upcoming = rig.mixer->UpcomingTracks(3);
    CHECK(upcoming.size() == 3);
    CHECK(upcoming[0].query == "song-b");
    CHECK(upcoming[1].query == "song-a");
    CHECK(upcoming[2].query == "song-b");

    CHECK(rig.queue.PopForPlayback()->query == "song-b");
    upcoming = rig.mixer->UpcomingTracks(3);
    CHECK(upcoming.size() == 3);
    CHECK(upcoming[0].query == "song-a");
    CHECK(upcoming[1].query == "song-b");
    CHECK(upcoming[2].query == "song-a");
  }

  {
    MixerRig rig;
    CHECK(rig.mixer->StartPlaylist({{"song-a"}, {"song-b"}}, "Toggle loop",
                                   false, true, 42));
    CHECK(rig.queue.PopForPlayback()->query == "song-a");
    CHECK(rig.mixer->UpcomingTracks(3).size() == 3);
    CHECK(!rig.mixer->SetPlaylistLoop(7, false));
    CHECK(rig.mixer->IsPlaylistLooping());
    CHECK(rig.mixer->SetPlaylistLoop(42, false));
    CHECK(!rig.mixer->IsPlaylistLooping());
    const auto remaining = rig.mixer->UpcomingTracks(3);
    CHECK(remaining.size() == 1);
    CHECK(remaining[0].query == "song-b");
    CHECK(remaining[0].origin == QueueOrigin::Playlist);
    CHECK(rig.queue.PopForPlayback()->query == "song-b");
    CHECK(rig.mixer->UpcomingTracks(3).empty());
  }

  {
    MixerRig rig;
    CHECK(rig.mixer->StartPlaylist({{"song-a"}, {"song-b"}}, "Once", false,
                                   false));
    CHECK(rig.queue.PopForPlayback()->query == "song-a");
    const auto upcoming = rig.mixer->UpcomingTracks(3);
    CHECK(upcoming.size() == 1 && upcoming[0].query == "song-b");
    CHECK(rig.queue.PopForPlayback()->query == "song-b");
    rig.mixer->NextSong();
    CHECK(rig.queue.Empty());
  }

  {
    MixerRig rig;
    vector<TrackRequest> songs;
    for (int index = 0; index < 20; ++index)
      songs.emplace_back("playlist-" + std::to_string(index));
    CHECK(rig.mixer->StartPlaylist(std::move(songs), "Full loop", false,
                                   true));
    CHECK(rig.queue.Size() == SongQueue::MAX_SONGS);
    CHECK(rig.mixer->AddToQueue("manual-next"));
    CHECK(rig.queue.Size() == SongQueue::MAX_SONGS);
    CHECK(rig.queue.Snapshot(1)[0].query == "manual-next");
    while (rig.queue.Size() > 2)
      CHECK(rig.queue.PopForPlayback());
    const auto replenished = rig.mixer->UpcomingTracks(3);
    CHECK(replenished.size() == 3);
    CHECK(replenished.back().query == "playlist-19");
  }

  {
    MixerRig rig(true);
    CHECK(rig.mixer->PlayNow("idle-play-now-regression"));
    const auto deadline = std::chrono::steady_clock::now() +
                          std::chrono::seconds(1);
    while (!rig.queue.Empty() && std::chrono::steady_clock::now() < deadline)
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
    CHECK(rig.queue.Empty());
  }

  {
    MixerRig rig;
    rig.playback.isPlaying.store(true);
    rig.playback.durationFrames.store(240 * 44100ULL);
    rig.playback.positionFrames.store(30 * 44100ULL);
    CHECK(rig.mixer->SeekToSeconds(90, false));
    CHECK(rig.playback.seekTargetFrames.load() == 90 * 44100);
    CHECK(rig.mixer->SeekRelativeSeconds(-45, false));
    CHECK(rig.playback.seekTargetFrames.load() == 45 * 44100);
    rig.playback.seekTargetFrames.store(-1);
    rig.playback.positionFrames.store(230 * 44100ULL);
    CHECK(rig.mixer->SeekRelativeSeconds(30, false));
    CHECK(rig.playback.seekTargetFrames.load() == 240 * 44100);
    rig.playback.isPaused.store(true);
    CHECK(rig.mixer->SeekToFraction(0.5, false));
    CHECK(rig.playback.seekTargetFrames.load() == 120 * 44100);
    rig.playback.isPlaying.store(false);
    CHECK(!rig.mixer->SeekToSeconds(10, false));
  }

  const auto volumeRoot =
      std::filesystem::temp_directory_path() /
      ("termify-mixer-volume-" +
       std::to_string(std::chrono::steady_clock::now()
                          .time_since_epoch()
                          .count()));
  std::filesystem::remove_all(volumeRoot);
  {
    LocalStore store(volumeRoot / "data", volumeRoot / "cache");
    store.SaveVolume(35);
    {
      MixerRig rig(false, &store);
      CHECK(rig.mixer->Volume() == 35);
      rig.mixer->SetVolume(55);
    }
    CHECK(store.SavedVolume() == 55);
  }
  std::filesystem::remove_all(volumeRoot);

  return 0;
}
