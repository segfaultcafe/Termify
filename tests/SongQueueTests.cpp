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
  const auto secondId = queue.Push("second", "Second title");
  CHECK(firstId);
  CHECK(secondId);
  CHECK(queue.Size() == 2);

  const auto snapshot = queue.Snapshot();
  CHECK(snapshot.size() == 2);
  CHECK(snapshot[0].query == "first");
  CHECK(snapshot[1].query == "second");
  CHECK(snapshot[1].title == "Second title");
  CHECK(snapshot[0].state == termify::core::QueueState::Pending);

  const auto claimed = queue.ClaimForPrefetch();
  CHECK(claimed && claimed->id == *firstId);
  CHECK(!queue.ReadyForPlayback());
  CHECK(queue.MarkReady(*firstId, "/tmp/first.opus", "https://example/first",
                        "Resolved first", true));
  CHECK(queue.ReadyForPlayback());
  const auto first = queue.PopForPlayback();
  CHECK(first && first->query == "first" &&
        first->source == "https://example/first" && first->temporary);
  CHECK(first->title == "Resolved first");
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

  CHECK(queue.PushPlaylistCycle(
      {{"playlist-a", "Playlist A"}, {"playlist-b", "Playlist B"}}));
  CHECK(!queue.PushPlaylistCycle({{"duplicate-cycle"}}));
  CHECK(queue.PushNext("manual-one"));
  CHECK(queue.PushNext("manual-two"));
  const auto prioritized = queue.Snapshot(4);
  CHECK(prioritized.size() == 4);
  CHECK(prioritized[0].query == "manual-two");
  CHECK(prioritized[1].query == "manual-one");
  CHECK(prioritized[2].query == "playlist-a");
  CHECK(prioritized[2].title == "Playlist A");
  CHECK(prioritized[3].query == "playlist-b");
  CHECK(prioritized[0].origin == termify::core::QueueOrigin::Manual);
  CHECK(prioritized[2].origin == termify::core::QueueOrigin::Playlist);

  const auto sameNext = queue.PrioritizeNext("manual-two");
  CHECK(sameNext.result == termify::core::QueueNextResult::AlreadyNext);
  CHECK(queue.Size() == 4);
  CHECK(queue.PrioritizeNext("manual-two", "Manual two title").result ==
        termify::core::QueueNextResult::AlreadyNext);
  CHECK(queue.Snapshot()[0].title == "Manual two title");
  const auto movedNext = queue.PrioritizeNext("playlist-b");
  CHECK(movedNext.result == termify::core::QueueNextResult::Moved);
  const auto deduplicated = queue.Snapshot(5);
  CHECK(deduplicated.size() == 4);
  CHECK(deduplicated[0].query == "playlist-b");
  CHECK(deduplicated[1].query == "manual-two");
  CHECK(std::count_if(deduplicated.begin(), deduplicated.end(),
                      [](const auto &song) {
                        return song.query == "playlist-b";
                      }) == 1);

  queue.Clear();
  CHECK(queue.Push("other"));
  const auto preparedId = queue.Push("prepared");
  CHECK(preparedId);
  CHECK(queue.ClaimForPrefetch());
  const auto claimedPrepared = queue.ClaimForPrefetch();
  CHECK(claimedPrepared && claimedPrepared->id == *preparedId);
  CHECK(queue.MarkReady(*preparedId, "/tmp/prepared", "https://prepared", {},
                        false));
  CHECK(queue.PrioritizeNext("prepared").result ==
        termify::core::QueueNextResult::Moved);
  const auto preserved = queue.Snapshot();
  CHECK(preserved[0].id == *preparedId);
  CHECK(preserved[0].state == termify::core::QueueState::Ready);
  CHECK(preserved[0].preparedPath == "/tmp/prepared");

  queue.Clear();
  CHECK(!queue.PushPlaylistCycle({{"valid"}, {""}}));
  CHECK(queue.Empty());

  CHECK(queue.AppendPlaylist({"loop-a", "Loop A"}));
  CHECK(queue.AppendPlaylist({"loop-b", "Loop B"}, true));
  CHECK(queue.Snapshot(2)[1].origin ==
        termify::core::QueueOrigin::PlaylistLoop);
  const auto continuations = queue.RemoveLoopContinuations();
  CHECK(continuations.size() == 1 && continuations[0].query == "loop-b");
  CHECK(queue.Size() == 1);
  CHECK(queue.AppendPlaylist({"loop-b", "Loop B"}));
  const auto dropped = queue.DropLastPlaylist();
  CHECK(dropped && dropped->query == "loop-b" && dropped->title == "Loop B");
  CHECK(queue.Size() == 1);
  queue.Clear();
  CHECK(!queue.DropLastPlaylist());

  const auto manualA = queue.PushNext("manual-a");
  const auto manualB = queue.PushNext("manual-b");
  CHECK(manualA && manualB);
  CHECK(queue.AppendPlaylist({"playlist-a", "Playlist A"}));
  CHECK(queue.AppendPlaylist({"playlist-b", "Playlist B"}, true));
  auto editable = queue.Snapshot(4);
  const auto playlistA = editable[2].id;
  const auto playlistB = editable[3].id;
  CHECK(queue.Move(*manualA, -1) ==
        termify::core::QueueMoveResult::Moved);
  CHECK(queue.Snapshot(4)[0].id == *manualA);
  CHECK(queue.Move(playlistA, -1) ==
        termify::core::QueueMoveResult::PriorityBoundary);
  CHECK(queue.Move(playlistB, -1) ==
        termify::core::QueueMoveResult::Moved);
  CHECK(queue.Snapshot(4)[2].id == playlistB);
  const auto removed = queue.Remove(playlistB);
  CHECK(removed && removed->query == "playlist-b");
  CHECK(queue.Size() == 3);
  const auto promoted = queue.PromoteToFront(playlistA);
  CHECK(promoted && promoted->id == playlistA);
  CHECK(promoted->origin == termify::core::QueueOrigin::Manual);
  CHECK(queue.Snapshot(1)[0].id == playlistA);
  CHECK(!queue.Remove(999999));
  CHECK(!queue.PromoteToFront(999999));
  return 0;
}
