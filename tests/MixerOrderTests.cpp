#include "Core/Mixer.hpp"

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

  MixerRig()
      : mixer(std::make_unique<Mixer>(&playback, &ringBuffer, &visualization,
                                      &responses, &queue)) {}
};

int main() {
  {
    MixerRig rig;
    CHECK(rig.mixer->StartPlaylist({"song-a", "song-b"}, "Test", false, false));
    const auto current = rig.queue.PopForPlayback();
    CHECK(current && current->query == "song-a");

    CHECK(rig.mixer->AddToQueue("darude sandstorm"));
    const auto upcoming = rig.queue.Snapshot();
    CHECK(upcoming.size() == 2);
    CHECK(upcoming[0].query == "darude sandstorm");
    CHECK(upcoming[0].origin == QueueOrigin::Manual);
    CHECK(upcoming[1].query == "song-b");
    CHECK(upcoming[1].origin == QueueOrigin::Playlist);
  }

  {
    MixerRig rig;
    CHECK(
        rig.mixer->StartPlaylist({"song-a", "song-b"}, "Looping", false, true));
    CHECK(rig.queue.PopForPlayback()->query == "song-a");

    rig.mixer->NextSong();
    auto upcoming = rig.queue.Snapshot();
    CHECK(upcoming.size() == 1);
    CHECK(upcoming[0].query == "song-b");

    CHECK(rig.queue.PopForPlayback()->query == "song-b");
    CHECK(rig.queue.Empty());
    rig.mixer->NextSong();
    upcoming = rig.queue.Snapshot();
    CHECK(upcoming.size() == 2);
    CHECK(upcoming[0].query == "song-a");
    CHECK(upcoming[1].query == "song-b");
  }

  {
    MixerRig rig;
    CHECK(rig.mixer->StartPlaylist({"song-a", "song-b"}, "Once", false, false));
    CHECK(rig.queue.PopForPlayback()->query == "song-a");
    CHECK(rig.queue.PopForPlayback()->query == "song-b");
    rig.mixer->NextSong();
    CHECK(rig.queue.Empty());
  }

  return 0;
}
