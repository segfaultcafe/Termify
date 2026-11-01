#include "Application.hpp"

#include "../Core/AudioDownloader.hpp"

#include <cmath>
#include <ctime>
#include <sys/ioctl.h>

namespace termify::app {

namespace {
constexpr tui::Style CYAN{tui::Color::Cyan};
constexpr tui::Style CYAN_BOLD{tui::Color::Cyan, true};
constexpr tui::Style MUTED{tui::Color::Muted};
constexpr tui::Style GREEN{tui::Color::Green};
constexpr tui::Style YELLOW{tui::Color::Yellow};
constexpr tui::Style RED{tui::Color::Red};
constexpr tui::Style BOLD{tui::Color::Default, true};

string addTrackMessage(core::AddTrackResult result) {
  switch (result) {
  case core::AddTrackResult::Added:
    return "Added the cached track to the playlist.";
  case core::AddTrackResult::HistoryNotFound:
    return "Play the track before adding it to a playlist.";
  case core::AddTrackResult::NotCached:
    return "That history item is no longer cached; play it again first.";
  case core::AddTrackResult::UnresolvedSource:
    return "This older cache has no verified URL; play it again while online.";
  case core::AddTrackResult::Duplicate:
    return "That track is already in this playlist.";
  case core::AddTrackResult::PlaylistFull:
    return "A playlist can contain at most 20 tracks.";
  }
  return "Could not add the track.";
}
} // namespace

Application::Application() {
  _playbackCtx.gRunning.store(true);
  _ringBufferCtx.ringBuffer.resize(_ringBufferCtx.CHANNELS *
                                   _ringBufferCtx.BUFFER_FRAMES);
  try {
    _store = std::make_unique<core::LocalStore>(
        core::LocalStore::DefaultDataDirectory(),
        core::LocalStore::DefaultCacheDirectory());
  } catch (const std::exception &error) {
    _statusMessage = "Local storage unavailable: " + string(error.what());
    _statusIsError = true;
  }
  _mixer = std::make_unique<core::Mixer>(&_playbackCtx, &_ringBufferCtx,
                                         &_visualizationCtx, &_mResponseCtx,
                                         &_trackQueue, _store.get());
  _cli = std::make_unique<core::Cli>(_mixer, [this](std::size_t tab) {
    std::lock_guard lock(_widgetMtx);
    _tabs->Select(tab);
    applyView();
    _widgets.Focus(tab == 1   ? "history-list"
                   : tab == 2 ? "playlist-list"
                   : tab == 3 ? "search-input"
                              : "command-input");
    _historyRefresh.store(true);
    _playlistRefresh.store(true);
  });

  _frame = &_widgets.Emplace<tui::Frame>("player-frame");
  _summary = &_widgets.Emplace<tui::Label>("playback-summary");
  _tabs = &_widgets.Emplace<tui::TabBar>(
      "tabs",
      std::vector<string>{"PLAYER", "HISTORY", "PLAYLISTS", "SEARCH"});
  _quit = &_widgets.Emplace<tui::Button>("quit", "QUIT");
  _historyRule = &_widgets.Emplace<tui::Rule>("history-rule");
  _historyList = &_widgets.Emplace<tui::ListView>("history-list");
  _historyPlayNow =
      &_widgets.Emplace<tui::Button>("history-play-now", "PLAY NOW");
  _historyPlayNext =
      &_widgets.Emplace<tui::Button>("history-play-next", "PLAY NEXT");
  _historyAdd =
      &_widgets.Emplace<tui::Button>("history-add", "ADD TO PLAYLIST");
  _historyRule->SetLabel("PLAY HISTORY");
  _historyList->SetEmptyText(
      _store ? "No plays yet. Play a song to start your history."
             : "Local storage is unavailable.");
  _historyList->SetDoubleClickActivates(true);
  _playlistRule = &_widgets.Emplace<tui::Rule>("playlist-rule");
  _playlistTrackRule = &_widgets.Emplace<tui::Rule>("playlist-track-rule");
  _playlistCommandsRule =
      &_widgets.Emplace<tui::Rule>("playlist-commands-rule");
  _trackCommandsRule = &_widgets.Emplace<tui::Rule>("track-commands-rule");
  _playlistList = &_widgets.Emplace<tui::ListView>("playlist-list");
  _playlistTrackList = &_widgets.Emplace<tui::ListView>("playlist-track-list");
  _playlistNew = &_widgets.Emplace<tui::Button>("playlist-new", "NEW");
  _playlistRename = &_widgets.Emplace<tui::Button>("playlist-rename", "RENAME");
  _playlistDelete = &_widgets.Emplace<tui::Button>("playlist-delete", "DELETE");
  _playlistAddCurrent =
      &_widgets.Emplace<tui::Button>("playlist-add-current", "ADD CURRENT");
  _playlistRemove = &_widgets.Emplace<tui::Button>("playlist-remove", "REMOVE");
  _playlistUp = &_widgets.Emplace<tui::Button>("playlist-up", "MOVE UP");
  _playlistDown = &_widgets.Emplace<tui::Button>("playlist-down", "MOVE DOWN");
  _playlistPlay =
      &_widgets.Emplace<tui::Button>("playlist-play", "PLAY SELECTED");
  _playlistShuffle =
      &_widgets.Emplace<tui::Button>("playlist-shuffle", "SHUFFLE OFF");
  _playlistLoop = &_widgets.Emplace<tui::Button>("playlist-loop", "LOOP OFF");
  _playlistRule->SetLabel("PLAYLISTS");
  _playlistTrackRule->SetLabel("TRACKS");
  _playlistCommandsRule->SetLabel("PLAYLIST COMMANDS");
  _trackCommandsRule->SetLabel("TRACK COMMANDS");
  _playlistList->SetEmptyText("No playlists. Choose NEW below.");
  _playlistTrackList->SetEmptyText("This playlist has no tracks.");
  _searchRule = &_widgets.Emplace<tui::Rule>("search-rule");
  _searchInput = &_widgets.Emplace<tui::TextInput>("search-input");
  _searchResultsRule = &_widgets.Emplace<tui::Rule>("search-results-rule");
  _playlistPickerRule =
      &_widgets.Emplace<tui::Rule>("playlist-picker-rule");
  _searchResults = &_widgets.Emplace<tui::ListView>("search-results");
  _searchPlayNow = &_widgets.Emplace<tui::Button>("search-play-now", "PLAY NOW");
  _searchPlayNext =
      &_widgets.Emplace<tui::Button>("search-play-next", "PLAY NEXT");
  _searchAddPlaylist = &_widgets.Emplace<tui::Button>(
      "search-add-playlist", "ADD TO PLAYLIST");
  _searchPlaylistList =
      &_widgets.Emplace<tui::ListView>("search-playlist-list");
  _searchPlaylistAdd =
      &_widgets.Emplace<tui::Button>("search-playlist-add", "ADD HERE");
  _searchPlaylistNew = &_widgets.Emplace<tui::Button>(
      "search-playlist-new", "NEW PLAYLIST");
  _searchPlaylistCancel =
      &_widgets.Emplace<tui::Button>("search-playlist-cancel", "CANCEL");
  _searchRule->SetLabel("YOUTUBE SEARCH");
  _searchInput->SetPrompt({{"search", CYAN_BOLD}, {" › ", {}}});
  _searchResultsRule->SetLabel("RESULTS");
  _searchResults->SetEmptyText("Type a query above and press Enter.");
  _searchResults->SetDoubleClickActivates(true);
  _searchPlaylistList->SetEmptyText(
      "No playlists yet. Choose NEW PLAYLIST below.");
  _nowPlayingRule = &_widgets.Emplace<tui::Rule>("now-playing-rule");
  _nowPlaying = &_widgets.Emplace<tui::Label>("now-playing");
  _details = &_widgets.Emplace<tui::Label>("track-details");
  _progress = &_widgets.Emplace<tui::ProgressBar>("track-progress");
  _playerPlayPause =
      &_widgets.Emplace<tui::Button>("player-play-pause", "|> PLAY");
  _playerNext = &_widgets.Emplace<tui::Button>("player-next", ">| NEXT");
  _playerStop = &_widgets.Emplace<tui::Button>("player-stop", "[] STOP");
  _playerRepeat =
      &_widgets.Emplace<tui::Button>("player-repeat", "↻ REPEAT OFF");
  _playerVolume = &_widgets.Emplace<tui::Knob>("player-volume");
  _queueRule = &_widgets.Emplace<tui::Rule>("queue-rule");
  _playerAddPlaylist = &_widgets.Emplace<tui::Button>(
      "player-add-playlist", "ADD TO PLAYLIST");
  for (std::size_t index = 0; index < _queueItems.size(); ++index) {
    _queueItems[index] =
        &_widgets.Emplace<tui::Label>("queue-item-" + std::to_string(index));
  }
  _waveformRule = &_widgets.Emplace<tui::Rule>("waveform-rule");
  _waveform = &_widgets.Emplace<tui::Label>("waveform");
  _separator = &_widgets.Emplace<tui::Rule>("status-rule");
  _status = &_widgets.Emplace<tui::Label>("status");
  _help = &_widgets.Emplace<tui::Label>("command-help");
  _input = &_widgets.Emplace<tui::TextInput>("command-input");

  _frame->SetTitle(
      {{" TERMIFY", CYAN_BOLD},
       {" - terminal audio player - v" TERMIFY_VERSION, MUTED}});
  _nowPlayingRule->SetLabel("NOW PLAYING");
  _queueRule->SetLabel("UP NEXT");
  _waveformRule->SetLabel("LIVE WAVEFORM");
  _input->SetPrompt({{"termify", CYAN_BOLD}, {" › ", {}}});
  _widgets.Focus("command-input");
  applyView();
}

Application::~Application() {
  if (_mixer) {
    _mixer->Shutdown();
  }
  _terminal.Leave();
}

void Application::Start() {
  if (!core::YtDlpManager::Self().Init()) {
    _statusMessage =
        "yt-dlp not found; local-file playback is still available.";
    _statusIsError = true;
  }

  _terminal.Enter();
  if (!_terminal.Interactive()) {
    runStreamInput();
    return;
  }

  std::thread visualization([this] { waveformLoop(); });
  try {
    runInteractiveInput();
  } catch (...) {
    _mixer->Shutdown();
    if (visualization.joinable()) {
      visualization.join();
    }
    _terminal.Leave();
    throw;
  }

  if (visualization.joinable()) {
    visualization.join();
  }
  _terminal.Leave();
}

void Application::waveformLoop() {
  while (_playbackCtx.gRunning.load()) {
    renderUi();
    std::this_thread::sleep_for(std::chrono::milliseconds(80));
  }
}

void Application::runInteractiveInput() {
  while (_playbackCtx.gRunning.load()) {
    auto event = _terminal.ReadEvent();
    if (!event) {
      _mixer->Shutdown();
      return;
    }

    if (const auto *key = std::get_if<tui::KeyEvent>(&*event);
        key &&
        (key->key == tui::Key::Interrupt || key->key == tui::Key::EndOfInput)) {
      _mixer->Shutdown();
      return;
    }

    if (auto *mouse = std::get_if<tui::MouseEvent>(&*event)) {
      mouse->column -= _visualizationCtx.uiLeftColumn.load();
      mouse->row -= _visualizationCtx.uiTopRow.load();
    }

    std::optional<string> submission;
    std::optional<string> searchSubmission;
    std::optional<core::TrackRequest> historyPlayNext;
    std::optional<core::TrackRequest> historyPlayNow;
    std::optional<core::TrackRequest> playlistReplay;
    bool cancelPrompt = false;
    bool createPlaylist = false, renamePlaylist = false;
    bool deletePlaylist = false, addHistory = false, addCurrent = false;
    bool removeTrack = false, moveUp = false, moveDown = false;
    bool playPlaylist = false, toggleShuffle = false, toggleLoop = false;
    bool togglePlayback = false, nextTrack = false, stopPlayback = false;
    bool toggleRepeatTrack = false;
    bool quitApplication = false;
    int volumeAdjustment = 0;
    int seekAdjustment = 0;
    std::optional<double> seekFraction;
    bool searchPlayNow = false, searchPlayNext = false;
    bool openSearchPicker = false, openPlayerPicker = false;
    bool addPickerTrack = false, createPickerPlaylist = false;
    bool closePicker = false;
    {
      std::lock_guard widgetLock(_widgetMtx);
      const auto oldTab = _tabs->Selected();
      const auto *key = std::get_if<tui::KeyEvent>(&*event);
      if (key && key->key == tui::Key::Escape &&
          _playlistPrompt != PlaylistPrompt::None) {
        cancelPrompt = true;
      } else if (key && key->key == tui::Key::Escape &&
                 (_searchPickerOpen || _playerPickerOpen)) {
        closePicker = true;
      } else if (key && key->key == tui::Key::Escape) {
        _widgets.Focus("command-input");
      } else if (key && key->modifiers.alt &&
                 (key->key == tui::Key::ArrowLeft ||
                  key->key == tui::Key::ArrowRight)) {
        _tabs->Handle(*event);
        _widgets.Focus("tabs");
      } else {
        _widgets.Dispatch(*event);
      }
      if (_tabs->Selected() != oldTab) {
        if (_playlistPrompt != PlaylistPrompt::None)
          cancelPrompt = true;
        applyView();
        _historyRefresh.store(true);
        _playlistRefresh.store(true);
      }
      if (const auto id = _historyList->TakeActivation()) {
        for (const auto &entry : _historyEntries) {
          if (std::to_string(entry.id) == *id) {
            historyPlayNext = core::TrackRequest(entry.source, entry.title);
            break;
          }
        }
      }
      if (const auto id = _historyList->TakeDoubleClickActivation()) {
        for (const auto &entry : _historyEntries) {
          if (std::to_string(entry.id) == *id) {
            historyPlayNow = core::TrackRequest(entry.source, entry.title);
            break;
          }
        }
      }
      if (const auto id = _playlistTrackList->TakeActivation()) {
        for (const auto &track : _playlistTracks) {
          if (std::to_string(track.id) == *id) {
            playlistReplay = core::TrackRequest(track.source, track.title);
            break;
          }
        }
      }
      if (_searchResults->TakeActivation())
        searchPlayNow = true;
      if (_searchResults->TakeDoubleClickActivation())
        searchPlayNow = true;
      if (_searchPlaylistList->TakeActivation())
        addPickerTrack = true;
      if (const auto selected = _playlistList->SelectedId()) {
        try {
          const auto id = std::stoll(*selected);
          if (id != _selectedPlaylistId) {
            _selectedPlaylistId = id;
            _deleteConfirmationId = 0;
            _playlistRefresh.store(true);
          }
        } catch (const std::exception &) {
        }
      }
      createPlaylist = _playlistNew->TakeActivation();
      renamePlaylist = _playlistRename->TakeActivation();
      deletePlaylist = _playlistDelete->TakeActivation();
      const auto selectedHistorySource =
          [this]() -> std::optional<core::TrackRequest> {
        const auto selected = _historyList->SelectedId();
        if (!selected)
          return std::nullopt;
        const auto entry = std::find_if(
            _historyEntries.begin(), _historyEntries.end(),
            [&selected](const auto &item) {
              return std::to_string(item.id) == *selected;
            });
        return entry == _historyEntries.end()
                   ? std::nullopt
                   : std::optional<core::TrackRequest>(
                         core::TrackRequest(entry->source, entry->title));
      };
      if (_historyPlayNow->TakeActivation())
        historyPlayNow = selectedHistorySource();
      if (_historyPlayNext->TakeActivation())
        historyPlayNext = selectedHistorySource();
      addHistory = _historyAdd->TakeActivation();
      addCurrent = _playlistAddCurrent->TakeActivation();
      removeTrack = _playlistRemove->TakeActivation();
      moveUp = _playlistUp->TakeActivation();
      moveDown = _playlistDown->TakeActivation();
      playPlaylist = _playlistPlay->TakeActivation();
      toggleShuffle = _playlistShuffle->TakeActivation();
      toggleLoop = _playlistLoop->TakeActivation();
      togglePlayback = _playerPlayPause->TakeActivation();
      nextTrack = _playerNext->TakeActivation();
      stopPlayback = _playerStop->TakeActivation();
      openPlayerPicker = _playerAddPlaylist->TakeActivation();
      toggleRepeatTrack = _playerRepeat->TakeActivation();
      volumeAdjustment = _playerVolume->TakeAdjustment();
      seekAdjustment = _progress->TakeSeekAdjustment();
      seekFraction = _progress->TakeSeekFraction();
      quitApplication = _quit->TakeActivation();
      searchPlayNow = _searchPlayNow->TakeActivation() || searchPlayNow;
      searchPlayNext = _searchPlayNext->TakeActivation();
      openSearchPicker = _searchAddPlaylist->TakeActivation();
      addPickerTrack =
          _searchPlaylistAdd->TakeActivation() || addPickerTrack;
      createPickerPlaylist = _searchPlaylistNew->TakeActivation();
      closePicker = _searchPlaylistCancel->TakeActivation() || closePicker;
      if (key && key->key == tui::Key::Character &&
          (key->text == "a" || key->text == "A") &&
          _widgets.FocusedId() == "history-list")
        addHistory = true;
      submission = _input->TakeSubmission();
      searchSubmission = _searchInput->TakeSubmission();
    }
    if (quitApplication) {
      _mixer->Shutdown();
      return;
    }
    if (cancelPrompt)
      cancelPlaylistPrompt();
    if (historyPlayNext)
      _mixer->AddToQueue(historyPlayNext->source, historyPlayNext->title);
    if (historyPlayNow)
      _mixer->PlayNow(historyPlayNow->source, historyPlayNow->title);
    if (playlistReplay)
      _mixer->AddToQueue(playlistReplay->source, playlistReplay->title);
    if (createPlaylist)
      beginPlaylistPrompt(false);
    if (renamePlaylist)
      beginPlaylistPrompt(true);
    if (deletePlaylist)
      deleteSelectedPlaylist();
    if (addHistory)
      addHistorySelection();
    if (addCurrent)
      addCurrentTrack();
    if (removeTrack)
      removePlaylistTrack();
    if (moveUp)
      movePlaylistTrack(-1);
    if (moveDown)
      movePlaylistTrack(1);
    if (playPlaylist)
      playSelectedPlaylist();
    if (toggleShuffle)
      togglePlaylistShuffle();
    if (toggleLoop)
      togglePlaylistLoop();
    if (togglePlayback) {
      if (_playbackCtx.isPaused.load())
        _mixer->Resume();
      else if (_playbackCtx.isPlaying.load())
        _mixer->Pause();
      else
        _mixer->PublishMessage(
            "Nothing is playing. Add a song with: play <song name>", true);
    }
    if (nextTrack)
      _mixer->NextSong();
    if (stopPlayback)
      _mixer->Stop(false, true);
    if (toggleRepeatTrack)
      _mixer->ToggleRepeat(!_mixer->IsRepeatOn());
    if (volumeAdjustment)
      _mixer->SetVolume(_mixer->Volume() + volumeAdjustment);
    if (seekFraction)
      _mixer->SeekToFraction(*seekFraction, false);
    if (seekAdjustment)
      _mixer->SeekRelativeSeconds(seekAdjustment);
    if (searchPlayNow) {
      if (const auto result = selectedSearchResult())
        _mixer->PlayNow(result->source, result->title);
      else
        _mixer->PublishMessage("Select a search result first.", true);
    }
    if (searchPlayNext) {
      if (const auto result = selectedSearchResult())
        _mixer->AddToQueue(result->source, result->title);
      else
        _mixer->PublishMessage("Select a search result first.", true);
    }
    if (openSearchPicker)
      openSearchPlaylistPicker();
    if (openPlayerPicker)
      openPlayerPlaylistPicker();
    if (addPickerTrack)
      addPickerTrackToPlaylist();
    if (createPickerPlaylist)
      beginPlaylistPrompt(false, true);
    if (closePicker)
      closePlaylistPicker();
    if (searchSubmission && !searchSubmission->empty())
      performSearch(*searchSubmission);
    if (submission) {
      if (_playlistPrompt != PlaylistPrompt::None)
        finishPlaylistPrompt(*submission);
      else if (!submission->empty())
        _cli->Execute(*submission);
    }
  }
}

void Application::runStreamInput() {
  string line;
  while (_playbackCtx.gRunning.load() && std::getline(std::cin, line)) {
    _cli->Execute(line);
  }
  if (_playbackCtx.gRunning.load()) {
    _mixer->Shutdown();
  }
}

void Application::configureWidgets(int width) {
  const int contentWidth = width - 6;
  constexpr int repeatControlWidth = 17;
  constexpr int volumeControlWidth = 17;
  constexpr int summaryControlGap = 1;
  constexpr int summaryControlsWidth =
      repeatControlWidth + volumeControlWidth + summaryControlGap;
  const bool playerView = _tabs->Selected() == 0 && !_playerPickerOpen;
  _tabs->SetBounds({0, 0, width, 3});
  _quit->SetBounds({width - 10, 3, 8, 1});
  _frame->SetBounds({0, 3, width, 16});
  _summary->SetBounds(
      {3, 4, playerView ? contentWidth - summaryControlsWidth : contentWidth,
       1});
  const int volumeColumn = width - 3 - volumeControlWidth;
  _playerRepeat->SetBounds(
      {volumeColumn - summaryControlGap - repeatControlWidth, 4,
       repeatControlWidth, 1});
  _playerVolume->SetBounds({volumeColumn, 4, volumeControlWidth, 1});
  _historyRule->SetBounds({0, 5, width, 1});
  _historyList->SetBounds({3, 6, contentWidth, 8});
  _historyPlayNow->SetBounds({3, 14, 14, 1});
  _historyPlayNext->SetBounds({18, 14, 15, 1});
  _historyAdd->SetBounds({34, 14, 22, 1});
  const int playlistColumn = std::max(25, width / 3);
  _playlistRule->SetBounds({0, 5, playlistColumn, 1});
  _playlistTrackRule->SetBounds({playlistColumn, 5, width - playlistColumn, 1});
  _playlistList->SetBounds({3, 6, playlistColumn - 5, 4});
  _playlistTrackList->SetBounds(
      {playlistColumn + 2, 6, width - playlistColumn - 5, 4});
  _playlistCommandsRule->SetBounds({0, 10, width, 1});
  _playlistNew->SetBounds({3, 11, 9, 1});
  _playlistRename->SetBounds({13, 11, 12, 1});
  _playlistDelete->SetBounds({26, 11, 12, 1});
  _playlistPlay->SetBounds({3, 12, 17, 1});
  _playlistShuffle->SetBounds({21, 12, 17, 1});
  _playlistLoop->SetBounds({39, 12, 14, 1});
  _trackCommandsRule->SetBounds({0, 13, width, 1});
  _playlistAddCurrent->SetBounds({3, 14, 17, 1});
  _playlistRemove->SetBounds({21, 14, 12, 1});
  _playlistUp->SetBounds({34, 14, 13, 1});
  _playlistDown->SetBounds({48, 14, 15, 1});
  _searchRule->SetBounds({0, 5, width, 1});
  _searchInput->SetBounds({3, 6, contentWidth, 1});
  _searchResultsRule->SetBounds({0, 7, width, 1});
  _playlistPickerRule->SetBounds({0, 7, width, 1});
  _searchResults->SetBounds({3, 8, contentWidth, 6});
  _searchPlaylistList->SetBounds({3, 8, contentWidth, 6});
  _searchPlayNow->SetBounds({3, 14, 14, 1});
  _searchPlayNext->SetBounds({18, 14, 15, 1});
  _searchAddPlaylist->SetBounds({34, 14, 22, 1});
  _searchPlaylistAdd->SetBounds({3, 14, 18, 1});
  _searchPlaylistNew->SetBounds({22, 14, 18, 1});
  _searchPlaylistCancel->SetBounds({41, 14, 12, 1});
  _nowPlayingRule->SetBounds({0, 5, width, 1});
  _nowPlaying->SetBounds({3, 6, contentWidth, 1});
  _details->SetBounds({3, 7, contentWidth, 1});
  _progress->SetBounds({3, 8, contentWidth, 1});
  constexpr int controlWidth = 12;
  constexpr int controlGap = 1;
  constexpr int controlBarWidth = controlWidth * 3 + controlGap * 2;
  int controlColumn = width - 3 - controlBarWidth;
  _playerPlayPause->SetBounds({controlColumn, 5, controlWidth, 1});
  controlColumn += controlWidth + controlGap;
  _playerNext->SetBounds({controlColumn, 5, controlWidth, 1});
  controlColumn += controlWidth + controlGap;
  _playerStop->SetBounds({controlColumn, 5, controlWidth, 1});
  _queueRule->SetBounds({0, 9, width, 1});
  _playerAddPlaylist->SetBounds({width - 22, 9, 19, 1});
  for (std::size_t index = 0; index < _queueItems.size(); ++index) {
    _queueItems[index]->SetBounds(
        {3, 10 + static_cast<int>(index), contentWidth, 1});
  }
  _waveformRule->SetBounds({0, 13, width, 1});
  _waveform->SetBounds({3, 14, contentWidth, 1});
  _separator->SetBounds({0, 15, width, 1});
  _status->SetBounds({3, 16, contentWidth, 1});
  _help->SetBounds({3, 17, contentWidth, 1});
  _input->SetBounds({0, 19, width, 1});
}

void Application::applyView() {
  const bool history = _tabs->Selected() == 1;
  const bool playlists = _tabs->Selected() == 2;
  const bool search = _tabs->Selected() == 3;
  const bool player = !history && !playlists && !search;
  const bool picker = (search && _searchPickerOpen) ||
                      (player && _playerPickerOpen);
  const bool searchActions =
      search && !_searchPickerOpen && !_searchLoading &&
      !_searchResultItems.empty();
  const bool historyActions = history && !_historyEntries.empty();
  _historyRule->SetVisible(history);
  _historyList->SetVisible(history);
  _historyPlayNow->SetVisible(historyActions);
  _historyPlayNext->SetVisible(historyActions);
  _historyAdd->SetVisible(historyActions);
  for (auto *widget : std::vector<tui::Widget *>{
           _playlistRule, _playlistTrackRule, _playlistCommandsRule,
           _trackCommandsRule, _playlistList, _playlistTrackList, _playlistNew,
           _playlistRename, _playlistDelete, _playlistAddCurrent,
           _playlistRemove, _playlistUp, _playlistDown, _playlistPlay,
           _playlistShuffle, _playlistLoop})
    widget->SetVisible(playlists);
  _searchRule->SetVisible(search);
  _searchInput->SetVisible(search);
  _searchResultsRule->SetVisible(search && !_searchPickerOpen);
  _playlistPickerRule->SetVisible(picker);
  _searchResults->SetVisible(search && !_searchPickerOpen);
  _searchPlayNow->SetVisible(searchActions);
  _searchPlayNext->SetVisible(searchActions);
  _searchAddPlaylist->SetVisible(searchActions);
  _searchPlaylistList->SetVisible(picker);
  _searchPlaylistAdd->SetVisible(picker);
  _searchPlaylistNew->SetVisible(picker);
  _searchPlaylistCancel->SetVisible(picker);
  for (auto *widget : std::vector<tui::Widget *>{
           _nowPlayingRule, _nowPlaying})
    widget->SetVisible(player);
  for (auto *widget : std::vector<tui::Widget *>{
           _details, _progress, _queueRule, _waveformRule, _waveform,
           _playerPlayPause, _playerNext, _playerStop, _playerAddPlaylist,
           _playerRepeat, _playerVolume})
    widget->SetVisible(player && !_playerPickerOpen);
  for (auto *item : _queueItems)
    item->SetVisible(player && !_playerPickerOpen);
  if (_widgets.FocusedId() &&
      _widgets.FocusedId()->starts_with("history-") && !historyActions &&
      _widgets.FocusedId() != "history-list")
    _widgets.Focus(history ? "history-list" : "tabs");
  if (!history && _widgets.FocusedId() == "history-list")
    _widgets.Focus("tabs");
  if (!playlists && _widgets.FocusedId() &&
      _widgets.FocusedId()->starts_with("playlist-"))
    _widgets.Focus("tabs");
  if (!player && _widgets.FocusedId() &&
      _widgets.FocusedId()->starts_with("player-"))
    _widgets.Focus("tabs");
  if (!search && !_playerPickerOpen && _widgets.FocusedId() &&
      _widgets.FocusedId()->starts_with("search-"))
    _widgets.Focus("tabs");
  if (search && !_searchPickerOpen && _widgets.FocusedId() &&
      _widgets.FocusedId()->starts_with("search-playlist-"))
    _widgets.Focus("search-results");
}

void Application::refreshHistory() {
  if (!_store)
    return;
  {
    std::lock_guard lock(_widgetMtx);
    if (_tabs->Selected() != 1)
      return;
  }
  const auto now = std::chrono::steady_clock::now();
  if (!_historyRefresh.exchange(false) &&
      _historyRevision == _store->Revision() && now < _historyNextPoll)
    return;
  _historyNextPoll = now + std::chrono::seconds(5);
  try {
    const auto revision = _store->Revision();
    auto entries = _store->History();
    std::vector<tui::ListItem> items;
    for (const auto &entry : entries) {
      const auto timestamp = static_cast<std::time_t>(entry.playedAt);
      std::tm time{};
      localtime_r(&timestamp, &time);
      char date[32]{};
      std::strftime(date, sizeof(date), "%m/%d %H:%M", &time);
      string label = string(date) + "  " +
                     (entry.cached ? "[cache] " : "        ") + entry.title +
                     (entry.artist.empty() ? "" : " — " + entry.artist);
      label.erase(
          std::remove_if(label.begin(), label.end(),
                         [](unsigned char ch) { return ch < 32 || ch == 127; }),
          label.end());
      items.push_back({std::to_string(entry.id), std::move(label)});
    }
    std::lock_guard lock(_widgetMtx);
    _historyEntries = std::move(entries);
    _historyList->SetItems(std::move(items));
    _historyRule->SetLabel("PLAY HISTORY · " +
                           std::to_string(_historyEntries.size()) +
                           (_historyEntries.size() == 1 ? " play" : " plays"));
    applyView();
    _historyRevision = revision;
  } catch (const std::exception &error) {
    _statusMessage = "Could not read history: " + string(error.what());
    _statusIsError = true;
  }
}

void Application::refreshPlaylists() {
  if (!_store)
    return;
  {
    std::lock_guard lock(_widgetMtx);
    if (_tabs->Selected() != 2)
      return;
  }
  const auto now = std::chrono::steady_clock::now();
  if (!_playlistRefresh.exchange(false) &&
      _playlistRevision == _store->Revision() && now < _playlistNextPoll)
    return;
  _playlistNextPoll = now + std::chrono::seconds(5);
  try {
    const auto revision = _store->Revision();
    auto playlists = _store->Playlists();
    auto selectedId = _selectedPlaylistId;
    if (std::none_of(
            playlists.begin(), playlists.end(),
            [selectedId](const auto &item) { return item.id == selectedId; }))
      selectedId = playlists.empty() ? 0 : playlists.front().id;
    auto tracks = selectedId ? _store->PlaylistTracks(selectedId)
                             : std::vector<core::PlaylistTrack>{};

    std::vector<tui::ListItem> playlistItems;
    for (const auto &playlist : playlists) {
      string modes;
      if (playlist.shuffle)
        modes += " ↝";
      if (playlist.loop)
        modes += " ↻";
      playlistItems.push_back({std::to_string(playlist.id),
                               playlist.name + " · " +
                                   std::to_string(playlist.trackCount) +
                                   modes});
    }
    std::vector<tui::ListItem> trackItems;
    for (const auto &track : tracks) {
      string label = track.cached ? "[cache] " : "[miss]  ";
      label += track.title;
      if (!track.artist.empty())
        label += " — " + track.artist;
      label.erase(
          std::remove_if(label.begin(), label.end(),
                         [](unsigned char ch) { return ch < 32 || ch == 127; }),
          label.end());
      trackItems.push_back({std::to_string(track.id), std::move(label)});
    }

    std::lock_guard lock(_widgetMtx);
    _selectedPlaylistId = selectedId;
    _playlists = std::move(playlists);
    _playlistTracks = std::move(tracks);
    _playlistList->SetItems(std::move(playlistItems));
    if (_selectedPlaylistId)
      _playlistList->SelectId(std::to_string(_selectedPlaylistId));
    _playlistTrackList->SetItems(std::move(trackItems));
    const auto selected = std::find_if(
        _playlists.begin(), _playlists.end(),
        [this](const auto &item) { return item.id == _selectedPlaylistId; });
    _playlistRule->SetLabel("PLAYLISTS · " + std::to_string(_playlists.size()));
    _playlistTrackRule->SetLabel(
        selected == _playlists.end()
            ? "TRACKS"
            : selected->name + " · " + std::to_string(selected->trackCount));
    _playlistShuffle->SetLabel(selected != _playlists.end() && selected->shuffle
                                   ? "SHUFFLE ON"
                                   : "SHUFFLE OFF");
    _playlistLoop->SetLabel(selected != _playlists.end() && selected->loop
                                ? "LOOP ON"
                                : "LOOP OFF");
    _playlistRevision = revision;
  } catch (const std::exception &error) {
    _statusMessage = "Could not read playlists: " + string(error.what());
    _statusIsError = true;
  }
}

std::optional<core::SearchResult> Application::selectedSearchResult() {
  std::lock_guard lock(_widgetMtx);
  const auto selected = _searchResults->SelectedId();
  if (!selected)
    return std::nullopt;
  try {
    const auto index = static_cast<std::size_t>(std::stoull(*selected));
    if (index < _searchResultItems.size())
      return _searchResultItems[index];
  } catch (const std::exception &) {
  }
  return std::nullopt;
}

void Application::performSearch(const string &query) {
  {
    std::lock_guard lock(_widgetMtx);
    _searchPickerOpen = false;
    _searchLoading = true;
    _searchResultsRule->SetLabel("RESULTS · searching...");
    _searchResults->SetEmptyText("Searching YouTube...");
    applyView();
  }
  _statusMessage = "Searching YouTube for '" + query + "'...";
  _statusIsError = false;
  try {
    auto response = core::YtDlpManager::Self().SearchYouTube(query, 10);
    std::vector<tui::ListItem> items;
    items.reserve(response.results.size());
    for (std::size_t index = 0; index < response.results.size(); ++index) {
      const auto &result = response.results[index];
      string label = std::to_string(index + 1) + ". " + result.title;
      if (!result.artist.empty())
        label += " — " + result.artist;
      if (result.durationSeconds > 0) {
        const auto minutes = result.durationSeconds / 60;
        const auto seconds = result.durationSeconds % 60;
        std::ostringstream duration;
        duration << "  " << minutes << ':' << std::setw(2)
                 << std::setfill('0') << seconds;
        label += duration.str();
      }
      items.push_back({std::to_string(index), std::move(label)});
    }
    const auto count = response.results.size();
    const bool cached = response.fromCache;
    {
      std::lock_guard lock(_widgetMtx);
      _searchResultItems = std::move(response.results);
      _searchResults->SetItems(std::move(items));
      _searchResults->SetEmptyText("No YouTube results found.");
      _searchLoading = false;
      _searchFromCache = cached;
      _searchResultsRule->SetLabel(
          "RESULTS · " + std::to_string(count) + (cached ? " · cached" : ""));
      applyView();
      if (count > 0)
        _widgets.Focus("search-results");
    }
    _statusMessage = std::to_string(count) + " YouTube result" +
                     (count == 1 ? "" : "s") +
                     (cached ? " loaded from the session cache." : " found.");
    _statusIsError = false;
  } catch (const std::exception &error) {
    std::lock_guard lock(_widgetMtx);
    _searchLoading = false;
    _searchFromCache = false;
    _searchResultItems.clear();
    _searchResults->SetItems({});
    _searchResults->SetEmptyText("Search failed. Edit the query and retry.");
    _searchResultsRule->SetLabel("RESULTS · error");
    applyView();
    _statusMessage = error.what();
    _statusIsError = true;
    _widgets.Focus("search-input");
  }
}

void Application::openSearchPlaylistPicker() {
  if (!_store) {
    _statusMessage = "Local storage is unavailable.";
    _statusIsError = true;
    return;
  }
  const auto result = selectedSearchResult();
  if (!result) {
    _statusMessage = "Select a search result first.";
    _statusIsError = true;
    return;
  }
  {
    std::lock_guard lock(_widgetMtx);
    _pickerTrack = core::HistoryEntry{
        0, result->source, result->title, result->artist, {},
        result->durationSeconds, 0, false};
    _pickerTrackVerified = true;
    _searchPickerOpen = true;
    _playerPickerOpen = false;
    applyView();
  }
  rebuildPlaylistPicker();
  std::lock_guard lock(_widgetMtx);
  _widgets.Focus(_searchPickerPlaylists.empty() ? "search-playlist-new"
                                                : "search-playlist-list");
}

void Application::openPlayerPlaylistPicker() {
  if (!_store) {
    _statusMessage = "Local storage is unavailable.";
    _statusIsError = true;
    return;
  }
  const auto source = _mixer->CurrentSource();
  if (source.empty()) {
    _statusMessage = "Nothing heard is currently playing.";
    _statusIsError = true;
    return;
  }
  core::TrackMetadata metadata;
  {
    std::lock_guard lock(_playbackCtx.metadataMtx);
    metadata = _playbackCtx.metadata;
  }
  if (metadata.title.empty())
    metadata.title = _mixer->TrackName();
  {
    std::lock_guard lock(_widgetMtx);
    _pickerTrack = core::HistoryEntry{
        0, source, metadata.title, metadata.artist, metadata.album,
        static_cast<std::int64_t>(_playbackCtx.durationFrames.load() / 44100),
        0, false};
    _pickerTrackVerified = false;
    _searchPickerOpen = false;
    _playerPickerOpen = true;
    applyView();
  }
  rebuildPlaylistPicker();
  std::lock_guard lock(_widgetMtx);
  _widgets.Focus(_searchPickerPlaylists.empty() ? "search-playlist-new"
                                                : "search-playlist-list");
}

void Application::rebuildPlaylistPicker() {
  std::optional<core::HistoryEntry> pickerTrack;
  {
    std::lock_guard lock(_widgetMtx);
    pickerTrack = _pickerTrack;
  }
  if (!_store || !pickerTrack)
    return;
  try {
    auto playlists = _store->Playlists();
    std::vector<std::int64_t> membership;
    std::vector<tui::ListItem> items;
    const auto sourceHash = core::LocalStore::SourceHash(pickerTrack->source);
    for (const auto &playlist : playlists) {
      const auto tracks = _store->PlaylistTracks(playlist.id);
      const bool contains =
          std::any_of(tracks.begin(), tracks.end(), [&](const auto &item) {
            return item.sourceHash == sourceHash ||
                   item.source == pickerTrack->source;
          });
      if (contains)
        membership.push_back(playlist.id);
      items.push_back({std::to_string(playlist.id),
                       string(contains ? "✓ " : "  ") + playlist.name +
                           " · " + std::to_string(playlist.trackCount)});
    }
    std::lock_guard lock(_widgetMtx);
    _searchPickerPlaylists = std::move(playlists);
    _searchPickerMembership = std::move(membership);
    _searchPlaylistList->SetItems(std::move(items));
    _playlistPickerRule->SetLabel(
        "ADD TO PLAYLIST · " +
        tui::Canvas::Clip(pickerTrack->title.empty() ? pickerTrack->source
                                                     : pickerTrack->title,
                          std::max(1, _lastUiWidth - 24)));
  } catch (const std::exception &error) {
    _statusMessage = "Could not read playlists: " + string(error.what());
    _statusIsError = true;
  }
}

void Application::addPickerTrackToPlaylist(std::int64_t playlistId) {
  std::optional<core::HistoryEntry> track;
  bool verified = false;
  string playlistName;
  {
    std::lock_guard lock(_widgetMtx);
    track = _pickerTrack;
    verified = _pickerTrackVerified;
    if (!playlistId) {
      const auto selected = _searchPlaylistList->SelectedId();
      try {
        if (selected)
          playlistId = std::stoll(*selected);
      } catch (const std::exception &) {
      }
    }
    const auto playlist = std::find_if(
        _searchPickerPlaylists.begin(), _searchPickerPlaylists.end(),
        [playlistId](const auto &item) { return item.id == playlistId; });
    if (playlist != _searchPickerPlaylists.end())
      playlistName = playlist->name;
  }
  if (!_store || !track || !playlistId) {
    _statusMessage = "Choose a playlist first.";
    _statusIsError = true;
    return;
  }
  try {
    const auto added = verified
                           ? _store->AddVerifiedSourceToPlaylist(playlistId,
                                                                 *track)
                           : _store->AddSourceToPlaylist(playlistId, *track);
    if (added == core::AddTrackResult::Added) {
      _statusMessage = "✓ Added to '" + playlistName + "'.";
      _statusIsError = false;
      _playlistRefresh.store(true);
    } else if (added == core::AddTrackResult::Duplicate) {
      _statusMessage = "✓ Already in '" + playlistName + "'.";
      _statusIsError = false;
    } else {
      _statusMessage = addTrackMessage(added);
      _statusIsError = true;
    }
    rebuildPlaylistPicker();
    std::lock_guard lock(_widgetMtx);
    _searchPlaylistList->SelectId(std::to_string(playlistId));
  } catch (const std::exception &error) {
    _statusMessage = "Could not add track: " + string(error.what());
    _statusIsError = true;
  }
}

void Application::closePlaylistPicker() {
  std::lock_guard lock(_widgetMtx);
  const bool searchPicker = _searchPickerOpen;
  const bool playerPicker = _playerPickerOpen;
  _searchPickerOpen = false;
  _playerPickerOpen = false;
  _pickerTrack.reset();
  _pickerTrackVerified = false;
  applyView();
  if (searchPicker)
    _widgets.Focus(_searchResultItems.empty() ? "search-input"
                                              : "search-results");
  else if (playerPicker)
    _widgets.Focus("player-add-playlist");
}

std::optional<core::Playlist> Application::selectedPlaylist() {
  std::lock_guard lock(_widgetMtx);
  const auto selected = std::find_if(
      _playlists.begin(), _playlists.end(),
      [this](const auto &item) { return item.id == _selectedPlaylistId; });
  return selected == _playlists.end()
             ? std::nullopt
             : std::optional<core::Playlist>(*selected);
}

void Application::beginPlaylistPrompt(bool rename, bool addPickerTrack) {
  if (!_store) {
    _statusMessage = "Local storage is unavailable.";
    _statusIsError = true;
    return;
  }
  auto playlist = selectedPlaylist();
  if (rename && !playlist) {
    _statusMessage = "Select a playlist to rename.";
    _statusIsError = true;
    return;
  }
  if (addPickerTrack) {
    std::lock_guard lock(_widgetMtx);
    if (!_pickerTrack) {
      _statusMessage = "Choose a track before creating its playlist.";
      _statusIsError = true;
      return;
    }
  }
  std::lock_guard lock(_widgetMtx);
  _playlistPrompt = addPickerTrack
                        ? PlaylistPrompt::CreateAndAdd
                        : (rename ? PlaylistPrompt::Rename
                                  : PlaylistPrompt::Create);
  _promptPlaylistId = rename ? playlist->id : 0;
  _input->Clear();
  _input->SetPrompt(
      {{rename ? "rename playlist" : "new playlist", CYAN_BOLD}, {" › ", {}}});
  _widgets.Focus("command-input");
  _statusMessage = rename ? "Type the new playlist name and press Enter."
                          : "Type a playlist name and press Enter.";
  _statusIsError = false;
}

void Application::finishPlaylistPrompt(const string &name) {
  const auto prompt = _playlistPrompt;
  const auto playlistId = _promptPlaylistId;
  try {
    std::int64_t createdId = 0;
    if (prompt == PlaylistPrompt::Create ||
        prompt == PlaylistPrompt::CreateAndAdd) {
      const auto id = _store->CreatePlaylist(name);
      createdId = id;
      {
        std::lock_guard lock(_widgetMtx);
        _selectedPlaylistId = id;
      }
      _statusMessage = "Playlist created.";
    } else if (prompt == PlaylistPrompt::Rename) {
      if (!_store->RenamePlaylist(playlistId, name))
        throw std::runtime_error("Playlist no longer exists");
      _statusMessage = "Playlist renamed.";
    } else {
      return;
    }
    _statusIsError = false;
    cancelPlaylistPrompt();
    _playlistRefresh.store(true);
    if (prompt == PlaylistPrompt::CreateAndAdd) {
      rebuildPlaylistPicker();
      addPickerTrackToPlaylist(createdId);
    }
  } catch (const std::exception &error) {
    _statusMessage = "Could not save playlist name: " + string(error.what());
    _statusIsError = true;
  }
}

void Application::cancelPlaylistPrompt() {
  std::lock_guard lock(_widgetMtx);
  const bool returnToPicker = _playlistPrompt == PlaylistPrompt::CreateAndAdd;
  _playlistPrompt = PlaylistPrompt::None;
  _promptPlaylistId = 0;
  _input->Clear();
  _input->SetPrompt({{"termify", CYAN_BOLD}, {" › ", {}}});
  if (returnToPicker && (_searchPickerOpen || _playerPickerOpen))
    _widgets.Focus(_searchPickerPlaylists.empty() ? "search-playlist-new"
                                                  : "search-playlist-list");
  else if (_tabs->Selected() == 2)
    _widgets.Focus("playlist-list");
}

void Application::addHistorySelection() {
  if (!_store)
    return;
  auto playlist = selectedPlaylist();
  std::int64_t historyId = 0;
  {
    std::lock_guard lock(_widgetMtx);
    if (const auto selected = _historyList->SelectedId()) {
      try {
        historyId = std::stoll(*selected);
      } catch (const std::exception &) {
      }
    }
  }
  if (!playlist || !historyId) {
    _statusMessage = playlist ? "Select a history item first."
                              : "Create or select a playlist first.";
    _statusIsError = true;
    return;
  }
  try {
    const auto result = _store->AddHistoryToPlaylist(playlist->id, historyId);
    _statusMessage = addTrackMessage(result);
    _statusIsError = result != core::AddTrackResult::Added;
    _playlistRefresh.store(true);
  } catch (const std::exception &error) {
    _statusMessage = "Could not add track: " + string(error.what());
    _statusIsError = true;
  }
}

void Application::addCurrentTrack() {
  if (!_store)
    return;
  const auto playlist = selectedPlaylist();
  const auto source = _mixer->CurrentSource();
  if (!playlist || source.empty()) {
    _statusMessage = playlist ? "Nothing heard is currently playing."
                              : "Select a playlist first.";
    _statusIsError = true;
    return;
  }
  core::TrackMetadata metadata;
  {
    std::lock_guard lock(_playbackCtx.metadataMtx);
    metadata = _playbackCtx.metadata;
  }
  try {
    const auto result = _store->AddSourceToPlaylist(
        playlist->id,
        {0, source, metadata.title, metadata.artist, metadata.album,
         static_cast<std::int64_t>(_playbackCtx.durationFrames.load() / 44100),
         0, false});
    _statusMessage = addTrackMessage(result);
    _statusIsError = result != core::AddTrackResult::Added;
    _playlistRefresh.store(true);
  } catch (const std::exception &error) {
    _statusMessage = "Could not add current track: " + string(error.what());
    _statusIsError = true;
  }
}

void Application::removePlaylistTrack() {
  auto playlist = selectedPlaylist();
  std::int64_t entryId = 0;
  {
    std::lock_guard lock(_widgetMtx);
    if (const auto selected = _playlistTrackList->SelectedId()) {
      try {
        entryId = std::stoll(*selected);
      } catch (const std::exception &) {
      }
    }
  }
  if (!playlist || !entryId) {
    _statusMessage = "Select a playlist track to remove.";
    _statusIsError = true;
    return;
  }
  try {
    if (_store->RemovePlaylistTrack(playlist->id, entryId)) {
      _statusMessage = "Track removed from playlist.";
      _statusIsError = false;
      _playlistRefresh.store(true);
    }
  } catch (const std::exception &error) {
    _statusMessage = "Could not remove track: " + string(error.what());
    _statusIsError = true;
  }
}

void Application::movePlaylistTrack(int direction) {
  auto playlist = selectedPlaylist();
  std::int64_t entryId = 0;
  {
    std::lock_guard lock(_widgetMtx);
    if (const auto selected = _playlistTrackList->SelectedId()) {
      try {
        entryId = std::stoll(*selected);
      } catch (const std::exception &) {
      }
    }
  }
  if (!playlist || !entryId ||
      !_store->MovePlaylistTrack(playlist->id, entryId, direction)) {
    _statusMessage = "The selected track cannot move farther.";
    _statusIsError = true;
    return;
  }
  _statusMessage = direction < 0 ? "Track moved up." : "Track moved down.";
  _statusIsError = false;
  _playlistRefresh.store(true);
}

void Application::playSelectedPlaylist() {
  const auto playlist = selectedPlaylist();
  vector<core::TrackRequest> tracks;
  {
    std::lock_guard lock(_widgetMtx);
    for (const auto &track : _playlistTracks)
      tracks.emplace_back(track.source, track.title);
  }
  if (!playlist) {
    _statusMessage = "Select a playlist first.";
    _statusIsError = true;
    return;
  }
  _mixer->StartPlaylist(std::move(tracks), playlist->name, playlist->shuffle,
                        playlist->loop, playlist->id);
}

void Application::togglePlaylistShuffle() {
  const auto playlist = selectedPlaylist();
  if (!playlist)
    return;
  try {
    _store->SetPlaylistShuffle(playlist->id, !playlist->shuffle);
    _statusMessage = playlist->shuffle ? "Playlist shuffle disabled."
                                       : "Playlist shuffle enabled.";
    _statusIsError = false;
    _playlistRefresh.store(true);
  } catch (const std::exception &error) {
    _statusMessage = "Could not change shuffle: " + string(error.what());
    _statusIsError = true;
  }
}

void Application::togglePlaylistLoop() {
  const auto playlist = selectedPlaylist();
  if (!playlist)
    return;
  try {
    const bool enabled = !playlist->loop;
    _store->SetPlaylistLoop(playlist->id, enabled);
    _mixer->SetPlaylistLoop(playlist->id, enabled);
    _statusMessage =
        playlist->loop ? "Playlist loop disabled." : "Playlist loop enabled.";
    _statusIsError = false;
    _playlistRefresh.store(true);
  } catch (const std::exception &error) {
    _statusMessage = "Could not change playlist loop: " + string(error.what());
    _statusIsError = true;
  }
}

void Application::deleteSelectedPlaylist() {
  const auto playlist = selectedPlaylist();
  if (!playlist)
    return;
  if (_deleteConfirmationId != playlist->id) {
    _deleteConfirmationId = playlist->id;
    _statusMessage = "Choose DELETE again to remove '" + playlist->name + "'.";
    _statusIsError = true;
    return;
  }
  try {
    _store->DeletePlaylist(playlist->id);
    {
      std::lock_guard lock(_widgetMtx);
      _selectedPlaylistId = 0;
      _deleteConfirmationId = 0;
    }
    _statusMessage = "Playlist deleted; cached audio was kept.";
    _statusIsError = false;
    _playlistRefresh.store(true);
  } catch (const std::exception &error) {
    _statusMessage = "Could not delete playlist: " + string(error.what());
    _statusIsError = true;
  }
}

void Application::renderUi() {
  readLatestMessage();
  if (_store) {
    const auto error = _store->TakeError();
    if (!error.empty()) {
      _statusMessage = "Could not save history: " + error;
      _statusIsError = true;
    }
  }
  refreshHistory();
  refreshPlaylists();

  const int columns = terminalWidth();
  const int rows = terminalHeight();
  if (columns < 68 || rows < 20) {
    std::lock_guard outputLock(_visualizationCtx.coutMtx);
    std::cout << "\033[2J";
    tui::Canvas notice(std::max(1, columns - 1), std::max(1, rows - 1));
    notice.Text({0, 0}, "Resize terminal to at least 68 x 20", YELLOW);
    notice.Present(std::cout, {1, 1});
    _lastUiWidth = 0;
    return;
  }
  const int width = std::min(120, std::max(64, columns - 4));
  const int left = std::max(1, (columns - width) / 2 + 1);
  const int top = std::max(1, (rows - 20) / 2 + 1);
  _visualizationCtx.uiLeftColumn.store(left);
  _visualizationCtx.uiTopRow.store(top);
  const int contentWidth = width - 6;
  const string track = _mixer->TrackName();
  const auto queue = _mixer->UpcomingTracks(3);
  const auto queueSize = _trackQueue.Size();
  const bool playlistLooping = _mixer->IsPlaylistLooping();
  const bool paused = _playbackCtx.isPaused.load();
  const bool playing = _playbackCtx.isPlaying.load();
  const bool loading = _playbackCtx.isLoading.load();
  core::TrackMetadata metadata;
  {
    std::lock_guard metadataLock(_playbackCtx.metadataMtx);
    metadata = _playbackCtx.metadata;
  }

  const string state =
      loading ? "LOADING"
              : (paused ? "PAUSED" : (playing ? "PLAYING" : "IDLE"));
  const tui::Style stateStyle =
      loading ? YELLOW : (paused ? YELLOW : (playing ? GREEN : MUTED));
  const string repeat = _mixer->IsRepeatOn() ? "ON" : "OFF";
  const string nowPlaying =
      track.empty() ? "Nothing playing"
                    : (metadata.title.empty() ? track : metadata.title);
  string details;
  if (loading) {
    details = "Fetching audio and metadata...";
  } else if (!metadata.artist.empty() && !metadata.album.empty()) {
    details = metadata.artist + " · " + metadata.album;
  } else if (!metadata.artist.empty()) {
    details = metadata.artist;
  } else if (!metadata.album.empty()) {
    details = metadata.album;
  }

  const uint64 position = _playbackCtx.positionFrames.load();
  const uint64 duration = _playbackCtx.durationFrames.load();
  const double ratio = duration > 0
                           ? std::clamp(static_cast<double>(position) /
                                            static_cast<double>(duration),
                                        0.0, 1.0)
                           : 0.0;

  std::lock_guard widgetLock(_widgetMtx);
  configureWidgets(width);
  string summaryDetails =
      "    QUEUE " +
      (playlistLooping ? string("∞") : std::to_string(queueSize));
  if (_tabs->Selected() != 0)
    summaryDetails += "    REPEAT " + repeat + "    VOL " +
                      std::to_string(_mixer->Volume()) + "%";
  _summary->SetSpans(
      {{"● " + state, stateStyle}, {std::move(summaryDetails), MUTED}});
  _nowPlaying->SetText(tui::Canvas::Clip(nowPlaying, contentWidth), BOLD);
  _details->SetText(tui::Canvas::Clip(details, contentWidth), MUTED);
  _progress->SetProgress(ratio);
  _progress->SetLabels(formatDuration(position),
                       duration > 0 ? formatDuration(duration) : "--:--");
  _playerPlayPause->SetLabel(playing && !paused ? "|| PAUSE" : "|> PLAY");
  _playerRepeat->SetLabel(_mixer->IsRepeatOn() ? "↻ REPEAT ON"
                                               : "↻ REPEAT OFF");
  _playerVolume->SetValue(_mixer->Volume());
  if (_searchPickerOpen || _playerPickerOpen) {
    bool alreadyAdded = false;
    if (const auto selected = _searchPlaylistList->SelectedId()) {
      try {
        const auto id = std::stoll(*selected);
        alreadyAdded =
            std::find(_searchPickerMembership.begin(),
                      _searchPickerMembership.end(), id) !=
            _searchPickerMembership.end();
      } catch (const std::exception &) {
      }
    }
    _searchPlaylistAdd->SetLabel(alreadyAdded ? "✓ IN PLAYLIST"
                                               : "✓ ADD HERE");
  }

  for (std::size_t index = 0; index < _queueItems.size(); ++index) {
    if (index < queue.size()) {
      const auto &queued = queue[index];
      string marker = "· ";
      tui::Style markerStyle = MUTED;
      if (queued.state == core::QueueState::Downloading) {
        marker = "↓ ";
        markerStyle = YELLOW;
      } else if (queued.state == core::QueueState::Ready) {
        marker = "✓ ";
        markerStyle = GREEN;
      } else if (queued.state == core::QueueState::Failed) {
        marker = "! ";
        markerStyle = RED;
      }
      string queueLabel = !queued.title.empty()
                              ? queued.title
                              : (!queued.source.empty() ? queued.source
                                                        : queued.query);
      queueLabel.erase(
          std::remove_if(queueLabel.begin(), queueLabel.end(),
                         [](unsigned char ch) { return ch < 32 || ch == 127; }),
          queueLabel.end());
      _queueItems[index]->SetSpans(
          {{marker, markerStyle},
           {tui::Canvas::Clip(queueLabel, contentWidth - 2), {}}});
    } else if (index == 0) {
      _queueItems[index]->SetText("Queue is empty", MUTED);
    } else {
      _queueItems[index]->SetText("");
    }
  }

  _waveform->SetText(buildWaveform(static_cast<std::size_t>(contentWidth)),
                     CYAN);
  const tui::Style statusStyle = _statusIsError ? RED : GREEN;
  _status->SetSpans(
      {{_statusIsError ? "! " : "› ", statusStyle},
       {tui::Canvas::Clip(_statusMessage, contentWidth - 2), statusStyle}});
  _help->SetText(
      tui::Canvas::Clip(
          _tabs->Selected() == 1
              ? "Enter: Play Next · double-click: Play Now · Tab: actions"
          : _tabs->Selected() == 2
              ? "Click/select lists · Tab reaches every action · Enter "
                "activates"
          : _tabs->Selected() == 3
              ? (_searchPickerOpen
                     ? "✓ membership · Add/New playlist · Esc cancels"
                     : "Type query · Enter search · select result · choose an "
                       "action")
              : (_playerPickerOpen
                     ? "✓ membership · Add/New playlist · Esc cancels"
                     : "Progress: click/drag/wheel seek · Volume wheel · Esc: "
                       "CLI"),
          contentWidth),
      MUTED);

  tui::Canvas canvas(width, 20);
  _widgets.Render(canvas);

  std::lock_guard outputLock(_visualizationCtx.coutMtx);
  if (_lastUiLeft != left || _lastUiTop != top || _lastUiWidth != width) {
    std::cout << "\033[2J";
    _lastUiLeft = left;
    _lastUiTop = top;
    _lastUiWidth = width;
  }
  canvas.Present(std::cout, {left, top}, _widgets.Cursor());
  return;
}

string Application::formatDuration(uint64 frames) {
  const uint64 seconds = frames / 44100;
  const uint64 hours = seconds / 3600;
  const uint64 minutes = (seconds % 3600) / 60;
  const uint64 remainder = seconds % 60;
  std::ostringstream output;
  if (hours > 0) {
    output << hours << ':' << std::setw(2) << std::setfill('0') << minutes;
  } else {
    output << minutes;
  }
  output << ':' << std::setw(2) << std::setfill('0') << remainder;
  return output.str();
}

void Application::readLatestMessage() {
  std::lock_guard lock(_mResponseCtx.resMtx);
  while (!_mResponseCtx.responses.empty()) {
    auto response = std::move(_mResponseCtx.responses.front());
    _mResponseCtx.responses.pop();
    _statusMessage = std::move(response.message);
    _statusIsError = response.isErr;
  }
}

string Application::buildWaveform(std::size_t width) {
  static constexpr const char *levels[] = {"·", "▁", "▂", "▃", "▄",
                                           "▅", "▆", "▇", "█"};
  vector<int> peaks(width, 0);
  int globalPeak = 0;

  {
    std::lock_guard lock(_visualizationCtx.visMtx);
    for (std::size_t column = 0; column < width; ++column) {
      const std::size_t begin =
          column * _visualizationCtx.visBuffer.size() / width;
      const std::size_t end =
          (column + 1) * _visualizationCtx.visBuffer.size() / width;
      for (std::size_t index = begin; index < end; ++index) {
        const int sample =
            std::abs(static_cast<int>(_visualizationCtx.visBuffer[index]));
        peaks[column] = std::max(peaks[column], sample);
      }
      globalPeak = std::max(globalPeak, peaks[column]);
    }
  }

  string output;
  for (const int peak : peaks) {
    const int level = globalPeak < 100 ? 0 : 1 + (peak * 7 / globalPeak);
    output += levels[std::clamp(level, 0, 8)];
  }
  return output;
}

int Application::terminalWidth() {
  winsize size{};
  if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &size) == 0 && size.ws_col > 0) {
    return size.ws_col;
  }
  return 80;
}

int Application::terminalHeight() {
  winsize size{};
  if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &size) == 0 && size.ws_row > 0) {
    return size.ws_row;
  }
  return 24;
}

} // namespace termify::app
