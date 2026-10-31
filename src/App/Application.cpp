#include "Application.hpp"

#include "../Core/AudioDownloader.hpp"

#include <cmath>
#include <stdexcept>
#include <sys/ioctl.h>
#include <unistd.h>

namespace termify::app {

namespace {
constexpr const char *RESET = "\033[0m";
constexpr const char *BOLD = "\033[1m";
constexpr const char *CYAN = "\033[38;5;81m";
constexpr const char *MUTED = "\033[38;5;244m";
constexpr const char *GREEN = "\033[38;5;84m";
constexpr const char *YELLOW = "\033[38;5;220m";
constexpr const char *RED = "\033[38;5;203m";

string repeatGlyph(const string &glyph, int count) {
  string output;
  for (int index = 0; index < count; ++index) {
    output += glyph;
  }
  return output;
}

string rule(int width, const string &label = {}) {
  if (label.empty()) {
    return "├" + repeatGlyph("─", width - 2);
  }
  const int fill = std::max(0, width - static_cast<int>(label.size()) - 5);
  return "├─ " + label + " " + repeatGlyph("─", fill);
}
} // namespace

Application::Application() {
  _playbackCtx.gRunning.store(true);
  _ringBufferCtx.ringBuffer.resize(_ringBufferCtx.CHANNELS *
                                   _ringBufferCtx.BUFFER_FRAMES);
  _mixer = std::make_unique<core::Mixer>(
      &_playbackCtx, &_ringBufferCtx, &_visualizationCtx, &_mResponseCtx,
      &_trackQueue);
  _cli = std::make_unique<core::Cli>(_mixer);
}

Application::~Application() {
  if (_mixer) {
    _mixer->Shutdown();
  }
  leaveTerminal();
}

void Application::Start() {
  if (!core::YtDlpManager::Self().Init()) {
    _statusMessage = "yt-dlp not found; local-file playback is still available.";
    _statusIsError = true;
  }

  enterTerminal();
  std::thread visualization([this] { waveformLoop(); });
  try {
    while (_playbackCtx.gRunning.load()) {
      _cli->Run();
    }
  } catch (...) {
    _mixer->Shutdown();
    if (visualization.joinable()) {
      visualization.join();
    }
    leaveTerminal();
    throw;
  }

  if (visualization.joinable()) {
    visualization.join();
  }
  leaveTerminal();
}

void Application::waveformLoop() {
  while (_playbackCtx.gRunning.load()) {
    renderUi();
    std::this_thread::sleep_for(std::chrono::milliseconds(80));
  }
}

void Application::renderUi() {
  readLatestMessage();

  const int columns = std::max(42, terminalWidth());
  const int rows = std::max(17, terminalHeight());
  const int width = std::min(120, std::max(42, columns - 4));
  const int left = std::max(1, (columns - width) / 2 + 1);
  const int top = std::max(1, (rows - 17) / 2 + 1);
  _visualizationCtx.uiLeftColumn.store(left);
  _visualizationCtx.uiTopRow.store(top);
  const std::size_t contentWidth = static_cast<std::size_t>(width - 4);
  const string track = _mixer->TrackName();
  const auto queue = _trackQueue.Snapshot(3);
  const auto queueSize = _trackQueue.Size();
  const bool paused = _playbackCtx.isPaused.load();
  const bool playing = _playbackCtx.isPlaying.load();
  const bool loading = _playbackCtx.isLoading.load();
  core::TrackMetadata metadata;
  {
    std::lock_guard metadataLock(_playbackCtx.metadataMtx);
    metadata = _playbackCtx.metadata;
  }

  const string state = loading ? "LOADING" :
                       (paused ? "PAUSED" : (playing ? "PLAYING" : "IDLE"));
  const char *stateColor = loading ? YELLOW :
                           (paused ? YELLOW : (playing ? GREEN : MUTED));
  const string repeat = _mixer->IsRepeatOn() ? "ON" : "OFF";
  const string nowPlaying = track.empty()
                                ? "Nothing playing"
                                : (metadata.title.empty() ? track
                                                          : metadata.title);
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
  string inputLine;
  std::size_t inputCursor = 0;
  {
    std::lock_guard inputLock(_visualizationCtx.inputMtx);
    inputLine = _visualizationCtx.inputLine;
    inputCursor = _visualizationCtx.inputCursor;
  }

  std::lock_guard outputLock(_visualizationCtx.coutMtx);
  if (_lastUiLeft != left || _lastUiTop != top) {
    std::cout << "\033[2J";
    _lastUiLeft = left;
    _lastUiTop = top;
  }
  auto line = [left, top](int row, const string &content) {
    std::cout << "\033[" << top + row - 1 << ';' << left << "H\033[2K"
              << content;
  };
  auto frameLine = [left, top, width](int row, const string &content,
                                      const string &rightEdge) {
    const int terminalRow = top + row - 1;
    std::cout << "\033[" << terminalRow << ';' << left << "H\033[2K"
              << content << "\033[" << terminalRow << ';'
              << left + width - 1 << 'H' << CYAN << rightEdge << RESET;
  };

  const string headerText = "╭─ TERMIFY ─ terminal audio player ";
  const int headerFill = std::max(
      0, width - 1 - static_cast<int>(textLength(headerText)));
  frameLine(1, string(CYAN) + BOLD + "╭─ TERMIFY" + RESET + MUTED +
                   " ─ terminal audio player " +
                   repeatGlyph("─", headerFill) + RESET,
            "╮");
  frameLine(2, "│  " + string(stateColor) + "● " + state + RESET + MUTED +
                   "    QUEUE " + std::to_string(queueSize) + "    REPEAT " +
                   repeat + "    VOL " + std::to_string(_mixer->Volume()) +
                   "%" + RESET,
            "│");
  frameLine(3, string(MUTED) + rule(width, "NOW PLAYING") + RESET, "┤");
  frameLine(4,
            "│  " + string(BOLD) + clipText(nowPlaying, contentWidth) + RESET,
            "│");
  frameLine(5, "│  " + string(MUTED) + clipText(details, contentWidth) + RESET,
            "│");
  frameLine(6, "│  " + buildProgress(contentWidth), "│");
  frameLine(7, string(MUTED) + rule(width, "UP NEXT") + RESET, "┤");

  for (int index = 0; index < 3; ++index) {
    string item;
    if (static_cast<std::size_t>(index) < queue.size()) {
      const auto &queued = queue[index];
      const char *marker = "·";
      const char *color = MUTED;
      if (queued.state == core::QueueState::Downloading) {
        marker = "↓";
        color = YELLOW;
      } else if (queued.state == core::QueueState::Ready) {
        marker = "✓";
        color = GREEN;
      } else if (queued.state == core::QueueState::Failed) {
        marker = "!";
        color = RED;
      }
      item = string(color) + marker + " " + RESET +
             clipText(queued.query, contentWidth - 2);
    } else if (index == 0) {
      item = string(MUTED) + "Queue is empty" + RESET;
    }
    frameLine(8 + index, "│  " + item, "│");
  }

  frameLine(11, string(MUTED) + rule(width, "LIVE WAVEFORM") + RESET, "┤");
  frameLine(12,
            "│  " + string(CYAN) + buildWaveform(contentWidth) + RESET,
            "│");
  frameLine(13, string(MUTED) + rule(width) + RESET, "┤");
  frameLine(14, "│  " + string(_statusIsError ? RED : GREEN) +
                    (_statusIsError ? "! " : "› ") +
                    clipText(_statusMessage, contentWidth - 2) + RESET,
            "│");
  frameLine(15, "│  " + string(MUTED) +
                    clipText(
                        "play · pause · next · repeat · volume · stop · help",
                        contentWidth) +
                    RESET,
            "│");
  frameLine(16, string(CYAN) + "╰" + repeatGlyph("─", width - 2) + RESET,
            "╯");
  line(core::VisualizationContext::PROMPT_ROW,
       string(CYAN) + "termify" + RESET + " \u203a " + inputLine);
  const std::size_t charactersBeforeCursor =
      textLength(inputLine.substr(0, inputCursor));
  std::cout << "\033[" << top + core::VisualizationContext::PROMPT_ROW - 1
            << ';'
            << left + 10 + charactersBeforeCursor << 'H' << std::flush;
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

string Application::buildProgress(std::size_t width) const {
  const uint64 position = _playbackCtx.positionFrames.load();
  const uint64 duration = _playbackCtx.durationFrames.load();
  const string current = formatDuration(position);
  const string total = duration > 0 ? formatDuration(duration) : "--:--";
  const std::size_t fixed = current.size() + total.size() + 4;
  const std::size_t barWidth = width > fixed ? width - fixed : 1;
  const double ratio = duration > 0
                           ? std::clamp(static_cast<double>(position) /
                                            static_cast<double>(duration),
                                        0.0, 1.0)
                           : 0.0;
  const std::size_t filled = static_cast<std::size_t>(ratio * barWidth);
  return string(MUTED) + current + " [" + RESET + CYAN +
         repeatGlyph("━", static_cast<int>(filled)) + MUTED +
         repeatGlyph("─", static_cast<int>(barWidth - filled)) + "] " + total +
         RESET;
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

void Application::enterTerminal() {
  if (isatty(STDIN_FILENO) &&
      tcgetattr(STDIN_FILENO, &_originalTermios) == 0) {
    termios inputMode = _originalTermios;
    inputMode.c_lflag &= static_cast<tcflag_t>(~(ICANON | ECHO));
    inputMode.c_iflag &= static_cast<tcflag_t>(~(IXON | ICRNL));
    inputMode.c_cc[VMIN] = 1;
    inputMode.c_cc[VTIME] = 0;
    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &inputMode) == 0) {
      _termiosActive = true;
    }
  }

  std::lock_guard lock(_visualizationCtx.coutMtx);
  std::cout << "\033[?1049h\033[2J\033[H\033[?25h" << std::flush;
  _terminalActive = true;
}

void Application::leaveTerminal() {
  if (!_terminalActive) {
    return;
  }
  std::lock_guard lock(_visualizationCtx.coutMtx);
  std::cout << "\033[?25h\033[?1049l" << std::flush;
  _terminalActive = false;
  if (_termiosActive) {
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &_originalTermios);
    _termiosActive = false;
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
        const int sample = std::abs(static_cast<int>(
            _visualizationCtx.visBuffer[index]));
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

string Application::clipText(const string &text, std::size_t maxCharacters) {
  if (textLength(text) <= maxCharacters) {
    return text;
  }
  if (maxCharacters <= 3) {
    return string(maxCharacters, '.');
  }

  std::size_t bytes = 0;
  std::size_t characters = 0;
  const std::size_t target = maxCharacters - 3;
  while (bytes < text.size() && characters < target) {
    const unsigned char lead = static_cast<unsigned char>(text[bytes]);
    std::size_t sequence = 1;
    if ((lead & 0xE0) == 0xC0)
      sequence = 2;
    else if ((lead & 0xF0) == 0xE0)
      sequence = 3;
    else if ((lead & 0xF8) == 0xF0)
      sequence = 4;
    bytes += std::min(sequence, text.size() - bytes);
    ++characters;
  }
  return text.substr(0, bytes) + "...";
}

std::size_t Application::textLength(const string &text) {
  return static_cast<std::size_t>(std::count_if(
      text.begin(), text.end(),
      [](unsigned char ch) { return (ch & 0xC0) != 0x80; }));
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
