#ifndef _TERMIFY_APP_APPLICATION_HPP
#define _TERMIFY_APP_APPLICATION_HPP

#include "../Core/Cli.hpp"
#include "../Tui/Navigation.hpp"
#include "../Tui/Terminal.hpp"
#include "../Tui/Widgets.hpp"

namespace termify::app {

class Application {
public:
  Application();
  ~Application();

  void Start();

private:
  void waveformLoop();
  void renderUi();
  void readLatestMessage();
  void runInteractiveInput();
  void runStreamInput();
  void configureWidgets(int width);
  void applyView();
  void refreshHistory();
  void refreshPlaylists();
  void beginPlaylistPrompt(bool rename);
  void finishPlaylistPrompt(const string &name);
  void cancelPlaylistPrompt();
  void addHistorySelection();
  void addCurrentTrack();
  void removePlaylistTrack();
  void movePlaylistTrack(int direction);
  void playSelectedPlaylist();
  void togglePlaylistShuffle();
  void togglePlaylistLoop();
  void deleteSelectedPlaylist();
  std::optional<core::Playlist> selectedPlaylist();
  string buildWaveform(std::size_t width);
  static string formatDuration(uint64 frames);
  static int terminalWidth();
  static int terminalHeight();

  core::PlaybackContext _playbackCtx;
  core::RingBufferContext _ringBufferCtx;
  core::VisualizationContext _visualizationCtx;
  core::MixerResponseContext _mResponseCtx;
  core::SongQueue _trackQueue;
  std::unique_ptr<core::LocalStore> _store;
  std::unique_ptr<core::Mixer> _mixer;
  std::unique_ptr<core::Cli> _cli;
  tui::Terminal _terminal;
  tui::WidgetHost _widgets;
  tui::Frame *_frame{nullptr};
  tui::Label *_summary{nullptr};
  tui::TabBar *_tabs{nullptr};
  tui::Rule *_historyRule{nullptr};
  tui::ListView *_historyList{nullptr};
  tui::Button *_historyAdd{nullptr};
  std::vector<core::HistoryEntry> _historyEntries;
  std::atomic_bool _historyRefresh{true};
  std::uint64_t _historyRevision{0};
  std::chrono::steady_clock::time_point _historyNextPoll{};
  tui::Rule *_playlistRule{nullptr};
  tui::Rule *_playlistTrackRule{nullptr};
  tui::ListView *_playlistList{nullptr};
  tui::ListView *_playlistTrackList{nullptr};
  tui::Button *_playlistNew{nullptr};
  tui::Button *_playlistRename{nullptr};
  tui::Button *_playlistDelete{nullptr};
  tui::Button *_playlistAddCurrent{nullptr};
  tui::Button *_playlistRemove{nullptr};
  tui::Button *_playlistUp{nullptr};
  tui::Button *_playlistDown{nullptr};
  tui::Button *_playlistPlay{nullptr};
  tui::Button *_playlistShuffle{nullptr};
  tui::Button *_playlistLoop{nullptr};
  std::vector<core::Playlist> _playlists;
  std::vector<core::PlaylistTrack> _playlistTracks;
  std::int64_t _selectedPlaylistId{0};
  std::atomic_bool _playlistRefresh{true};
  std::uint64_t _playlistRevision{0};
  std::chrono::steady_clock::time_point _playlistNextPoll{};
  enum class PlaylistPrompt { None, Create, Rename };
  PlaylistPrompt _playlistPrompt{PlaylistPrompt::None};
  std::int64_t _promptPlaylistId{0};
  std::int64_t _deleteConfirmationId{0};
  tui::Rule *_nowPlayingRule{nullptr};
  tui::Label *_nowPlaying{nullptr};
  tui::Label *_details{nullptr};
  tui::ProgressBar *_progress{nullptr};
  tui::Button *_playerPlayPause{nullptr};
  tui::Button *_playerNext{nullptr};
  tui::Button *_playerStop{nullptr};
  tui::Rule *_queueRule{nullptr};
  std::array<tui::Label *, 3> _queueItems{};
  tui::Rule *_waveformRule{nullptr};
  tui::Label *_waveform{nullptr};
  tui::Rule *_separator{nullptr};
  tui::Label *_status{nullptr};
  tui::Label *_help{nullptr};
  tui::TextInput *_input{nullptr};
  std::mutex _widgetMtx;
  string _statusMessage{"Ready. Add a track with: play <song name>"};
  bool _statusIsError{false};
  int _lastUiLeft{0};
  int _lastUiTop{0};
  int _lastUiWidth{0};
};

} // namespace termify::app

#endif // _TERMIFY_APP_APPLICATION_HPP
