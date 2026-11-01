#ifndef _TERMIFY_APP_APPLICATION_HPP
#define _TERMIFY_APP_APPLICATION_HPP

#include "../Core/Cli.hpp"
#include "../Core/AudioDownloader.hpp"
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
  void performSearch(const string &query);
  void openSearchPlaylistPicker();
  void openPlayerPlaylistPicker();
  void closePlaylistPicker();
  void addPickerTrackToPlaylist(std::int64_t playlistId = 0);
  std::optional<core::SearchResult> selectedSearchResult();
  void rebuildPlaylistPicker();
  void beginPlaylistPrompt(bool rename, bool addPickerTrack = false);
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
  tui::Button *_quit{nullptr};
  tui::Rule *_historyRule{nullptr};
  tui::ListView *_historyList{nullptr};
  tui::Button *_historyPlayNow{nullptr};
  tui::Button *_historyPlayNext{nullptr};
  tui::Button *_historyAdd{nullptr};
  std::vector<core::HistoryEntry> _historyEntries;
  std::atomic_bool _historyRefresh{true};
  std::uint64_t _historyRevision{0};
  std::chrono::steady_clock::time_point _historyNextPoll{};
  tui::Rule *_playlistRule{nullptr};
  tui::Rule *_playlistTrackRule{nullptr};
  tui::Rule *_playlistCommandsRule{nullptr};
  tui::Rule *_trackCommandsRule{nullptr};
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
  enum class PlaylistPrompt { None, Create, Rename, CreateAndAdd };
  PlaylistPrompt _playlistPrompt{PlaylistPrompt::None};
  std::int64_t _promptPlaylistId{0};
  std::int64_t _deleteConfirmationId{0};
  tui::Rule *_searchRule{nullptr};
  tui::TextInput *_searchInput{nullptr};
  tui::Rule *_searchResultsRule{nullptr};
  tui::Rule *_playlistPickerRule{nullptr};
  tui::ListView *_searchResults{nullptr};
  tui::Button *_searchPlayNow{nullptr};
  tui::Button *_searchPlayNext{nullptr};
  tui::Button *_searchAddPlaylist{nullptr};
  tui::ListView *_searchPlaylistList{nullptr};
  tui::Button *_searchPlaylistAdd{nullptr};
  tui::Button *_searchPlaylistNew{nullptr};
  tui::Button *_searchPlaylistCancel{nullptr};
  std::vector<core::SearchResult> _searchResultItems;
  std::vector<core::Playlist> _searchPickerPlaylists;
  std::vector<std::int64_t> _searchPickerMembership;
  std::optional<core::HistoryEntry> _pickerTrack;
  bool _pickerTrackVerified{false};
  bool _searchPickerOpen{false};
  bool _playerPickerOpen{false};
  bool _searchLoading{false};
  bool _searchFromCache{false};
  tui::Rule *_nowPlayingRule{nullptr};
  tui::Label *_nowPlaying{nullptr};
  tui::Label *_details{nullptr};
  tui::ProgressBar *_progress{nullptr};
  tui::Button *_playerPlayPause{nullptr};
  tui::Button *_playerNext{nullptr};
  tui::Button *_playerStop{nullptr};
  tui::Button *_playerAddPlaylist{nullptr};
  tui::Button *_playerRepeat{nullptr};
  tui::Knob *_playerVolume{nullptr};
  tui::Rule *_queueRule{nullptr};
  tui::ListView *_queueList{nullptr};
  tui::Button *_queuePlayNow{nullptr};
  tui::Button *_queueRemove{nullptr};
  tui::Button *_queueUp{nullptr};
  tui::Button *_queueDown{nullptr};
  tui::Button *_queueClear{nullptr};
  bool _queueClearArmed{false};
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
