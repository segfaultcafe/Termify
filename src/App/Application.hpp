#ifndef _TERMIFY_APP_APPLICATION_HPP
#define _TERMIFY_APP_APPLICATION_HPP

#include "../Core/Cli.hpp"
#include <termios.h>

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
  void enterTerminal();
  void leaveTerminal();
  string buildWaveform(std::size_t width);
  string buildProgress(std::size_t width) const;
  static string formatDuration(uint64 frames);
  static string clipText(const string &text, std::size_t maxCharacters);
  static std::size_t textLength(const string &text);
  static int terminalWidth();
  static int terminalHeight();

  core::PlaybackContext _playbackCtx;
  core::RingBufferContext _ringBufferCtx;
  core::VisualizationContext _visualizationCtx;
  core::MixerResponseContext _mResponseCtx;
  core::SongQueue _trackQueue;
  std::unique_ptr<core::Mixer> _mixer;
  std::unique_ptr<core::Cli> _cli;
  string _statusMessage{"Ready. Add a track with: play <song name>"};
  bool _statusIsError{false};
  bool _terminalActive{false};
  bool _termiosActive{false};
  termios _originalTermios{};
  int _lastUiLeft{0};
  int _lastUiTop{0};
};

} // namespace termify::app

#endif // _TERMIFY_APP_APPLICATION_HPP
