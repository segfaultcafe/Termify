#include "SongQueue.hpp"

#include <iostream>

using termify::core::SongQueue;

#define CHECK(condition)                                                       \
  do {                                                                         \
    if (!(condition)) {                                                        \
      std::cerr << "Check failed at line " << __LINE__ << ": " #condition      \
                << '\n';                                                       \
      return 1;                                                                \
    }                                                                          \
  } while (false)

int main() {
  SongQueue queue;
  CHECK(queue.Empty());
  CHECK(!queue.Push(""));

  const auto firstId = queue.Push("first");
  const auto secondId = queue.Push("second");
  CHECK(firstId);
  CHECK(secondId);
  CHECK(queue.Size() == 2);

  const auto snapshot = queue.Snapshot();
  CHECK(snapshot.size() == 2);
  CHECK(snapshot[0].query == "first");
  CHECK(snapshot[1].query == "second");
  CHECK(snapshot[0].state == termify::core::QueueState::Pending);

  const auto claimed = queue.ClaimForPrefetch();
  CHECK(claimed && claimed->id == *firstId);
  CHECK(!queue.ReadyForPlayback());
  CHECK(queue.MarkReady(*firstId, "/tmp/first.opus", "https://example/first",
                        true));
  CHECK(queue.ReadyForPlayback());
  const auto first = queue.PopForPlayback();
  CHECK(first && first->query == "first" &&
        first->source == "https://example/first" && first->temporary);
  const auto second = queue.PopForPlayback();
  CHECK(second && second->query == "second");
  CHECK(queue.Empty());

  const auto failedId = queue.Push("broken");
  CHECK(failedId);
  CHECK(queue.ClaimForPrefetch());
  CHECK(queue.MarkFailed(*failedId, "network unavailable"));
  CHECK(queue.ReadyForPlayback());
  const auto failed = queue.PopForPlayback();
  CHECK(failed && failed->state == termify::core::QueueState::Failed);
  CHECK(failed->error == "network unavailable");

  for (std::size_t index = 0; index < SongQueue::MAX_SONGS; ++index) {
    CHECK(queue.Push(std::to_string(index)).has_value());
  }
  CHECK(!queue.Push("overflow"));
  CHECK(queue.Size() == SongQueue::MAX_SONGS);

  const auto cleared = queue.Clear();
  CHECK(cleared.size() == SongQueue::MAX_SONGS);
  CHECK(queue.Empty());

  CHECK(queue.PushPlaylistCycle({"playlist-a", "playlist-b"}));
  CHECK(!queue.PushPlaylistCycle({"duplicate-cycle"}));
  CHECK(queue.PushNext("manual-one"));
  CHECK(queue.PushNext("manual-two"));
  const auto prioritized = queue.Snapshot(4);
  CHECK(prioritized.size() == 4);
  CHECK(prioritized[0].query == "manual-one");
  CHECK(prioritized[1].query == "manual-two");
  CHECK(prioritized[2].query == "playlist-a");
  CHECK(prioritized[3].query == "playlist-b");
  CHECK(prioritized[0].origin == termify::core::QueueOrigin::Manual);
  CHECK(prioritized[2].origin == termify::core::QueueOrigin::Playlist);

  queue.Clear();
  CHECK(!queue.PushPlaylistCycle({"valid", ""}));
  CHECK(queue.Empty());
  return 0;
}
