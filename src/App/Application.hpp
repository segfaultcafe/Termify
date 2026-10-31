#ifndef _TERMIFY_APP_APPLICATION_HPP
#define _TERMIFY_APP_APPLICATION_HPP

#include "../Core/Cli.hpp"
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
  string buildWaveform(std::size_t width);
  static string formatDuration(uint64 frames);
  static int terminalWidth();
  static int terminalHeight();

  core::PlaybackContext _playbackCtx;
  core::RingBufferContext _ringBufferCtx;
  core::VisualizationContext _visualizationCtx;
  core::MixerResponseContext _mResponseCtx;
  core::SongQueue _trackQueue;
  std::unique_ptr<core::Mixer> _mixer;
  std::unique_ptr<core::Cli> _cli;
  tui::Terminal _terminal;
  tui::WidgetHost _widgets;
  tui::Frame *_frame{nullptr};
  tui::Label *_summary{nullptr};
  tui::Rule *_nowPlayingRule{nullptr};
  tui::Label *_nowPlaying{nullptr};
  tui::Label *_details{nullptr};
  tui::ProgressBar *_progress{nullptr};
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
};

} // namespace termify::app

#endif // _TERMIFY_APP_APPLICATION_HPP
