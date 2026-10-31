#include "Application.hpp"

#include "../Core/AudioDownloader.hpp"

#include <cmath>
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
} // namespace

Application::Application() {
  _playbackCtx.gRunning.store(true);
  _ringBufferCtx.ringBuffer.resize(_ringBufferCtx.CHANNELS *
                                   _ringBufferCtx.BUFFER_FRAMES);
  _mixer = std::make_unique<core::Mixer>(
      &_playbackCtx, &_ringBufferCtx, &_visualizationCtx, &_mResponseCtx,
      &_trackQueue);
  _cli = std::make_unique<core::Cli>(_mixer);

  _frame = &_widgets.Emplace<tui::Frame>("player-frame");
  _summary = &_widgets.Emplace<tui::Label>("playback-summary");
  _nowPlayingRule = &_widgets.Emplace<tui::Rule>("now-playing-rule");
  _nowPlaying = &_widgets.Emplace<tui::Label>("now-playing");
  _details = &_widgets.Emplace<tui::Label>("track-details");
  _progress = &_widgets.Emplace<tui::ProgressBar>("track-progress");
  _queueRule = &_widgets.Emplace<tui::Rule>("queue-rule");
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

  _frame->SetTitle({{" TERMIFY", CYAN_BOLD},
                    {" ─ terminal audio player", MUTED}});
  _nowPlayingRule->SetLabel("NOW PLAYING");
  _queueRule->SetLabel("UP NEXT");
  _waveformRule->SetLabel("LIVE WAVEFORM");
  _input->SetPrompt({{"termify", CYAN_BOLD}, {" › ", {}}});
  _widgets.Focus("command-input");
}

Application::~Application() {
  if (_mixer) {
    _mixer->Shutdown();
  }
  _terminal.Leave();
}

void Application::Start() {
  if (!core::YtDlpManager::Self().Init()) {
    _statusMessage = "yt-dlp not found; local-file playback is still available.";
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
        key && (key->key == tui::Key::Interrupt ||
                key->key == tui::Key::EndOfInput)) {
      _mixer->Shutdown();
      return;
    }

    if (auto *mouse = std::get_if<tui::MouseEvent>(&*event)) {
      mouse->column -= _visualizationCtx.uiLeftColumn.load();
      mouse->row -= _visualizationCtx.uiTopRow.load();
    }

    std::optional<string> submission;
    {
      std::lock_guard widgetLock(_widgetMtx);
      _widgets.Dispatch(*event);
      submission = _input->TakeSubmission();
    }
    if (submission && !submission->empty()) {
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
  _frame->SetBounds({0, 0, width, 16});
  _summary->SetBounds({3, 1, contentWidth, 1});
  _nowPlayingRule->SetBounds({0, 2, width, 1});
  _nowPlaying->SetBounds({3, 3, contentWidth, 1});
  _details->SetBounds({3, 4, contentWidth, 1});
  _progress->SetBounds({3, 5, contentWidth, 1});
  _queueRule->SetBounds({0, 6, width, 1});
  for (std::size_t index = 0; index < _queueItems.size(); ++index) {
    _queueItems[index]->SetBounds(
        {3, 7 + static_cast<int>(index), contentWidth, 1});
  }
  _waveformRule->SetBounds({0, 10, width, 1});
  _waveform->SetBounds({3, 11, contentWidth, 1});
  _separator->SetBounds({0, 12, width, 1});
  _status->SetBounds({3, 13, contentWidth, 1});
  _help->SetBounds({3, 14, contentWidth, 1});
  _input->SetBounds({0, 16, width, 1});
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
  const int contentWidth = width - 6;
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
  const tui::Style stateStyle =
      loading ? YELLOW : (paused ? YELLOW : (playing ? GREEN : MUTED));
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

  const uint64 position = _playbackCtx.positionFrames.load();
  const uint64 duration = _playbackCtx.durationFrames.load();
  const double ratio = duration > 0
                           ? std::clamp(static_cast<double>(position) /
                                            static_cast<double>(duration),
                                        0.0, 1.0)
                           : 0.0;

  std::lock_guard widgetLock(_widgetMtx);
  configureWidgets(width);
  _summary->SetSpans({{"● " + state, stateStyle},
                      {"    QUEUE " + std::to_string(queueSize) +
                           "    REPEAT " + repeat + "    VOL " +
                           std::to_string(_mixer->Volume()) + "%",
                       MUTED}});
  _nowPlaying->SetText(tui::Canvas::Clip(nowPlaying, contentWidth), BOLD);
  _details->SetText(tui::Canvas::Clip(details, contentWidth), MUTED);
  _progress->SetProgress(ratio);
  _progress->SetLabels(formatDuration(position),
                       duration > 0 ? formatDuration(duration) : "--:--");

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
      _queueItems[index]->SetSpans(
          {{marker, markerStyle},
           {tui::Canvas::Clip(queued.query, contentWidth - 2), {}}});
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
  _help->SetText(tui::Canvas::Clip(
                     "play · pause · next · repeat · volume · stop · help",
                     contentWidth),
                 MUTED);

  tui::Canvas canvas(width, core::VisualizationContext::PROMPT_ROW);
  _widgets.Render(canvas);

  std::lock_guard outputLock(_visualizationCtx.coutMtx);
  if (_lastUiLeft != left || _lastUiTop != top) {
    std::cout << "\033[2J";
    _lastUiLeft = left;
    _lastUiTop = top;
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
