#include "Cli.hpp"

#include <cerrno>
#include <cctype>
#include <unistd.h>

namespace termify::core {

namespace {
std::optional<string> commandHelp(const string &topic) {
  switch (StringToCommand(topic)) {
  case Command::PLAY:
    return "play <song, URL, or local file> — add a track to the queue";
  case Command::PAUSE:
    return "pause — pause the current track";
  case Command::RESUME:
    return "resume — continue a paused track";
  case Command::NEXT:
    return "next — skip to the next queued track";
  case Command::REPEAT:
    return "repeat — toggle repeating the current track";
  case Command::VOLUME:
    return "volume <0-100> to set volume; volume <+N|-N> to adjust it";
  case Command::QUEUE:
    return "queue — show how many tracks are waiting";
  case Command::CLEAR:
    return "clear — remove all upcoming tracks";
  case Command::STOP:
    return "stop — stop playback and clear the queue";
  case Command::HELP:
    return "help [command] — show all commands or detailed command usage";
  case Command::EXIT:
    return "exit — close Termify cleanly";
  case Command::UNKNOWN:
    return std::nullopt;
  }
  return std::nullopt;
}
} // namespace

Command StringToCommand(string command) {
  std::transform(command.begin(), command.end(), command.begin(),
                 [](unsigned char ch) { return std::tolower(ch); });

  if (command == "exit" || command == "quit" || command == ".q")
    return Command::EXIT;
  if (command == "play" || command == ".p")
    return Command::PLAY;
  if (command == "stop" || command == ".s")
    return Command::STOP;
  if (command == "repeat" || command == "loop" || command == ".l")
    return Command::REPEAT;
  if (command == "pause" || command == ".pause")
    return Command::PAUSE;
  if (command == "resume" || command == ".r")
    return Command::RESUME;
  if (command == "next" || command == "skip" || command == ".n")
    return Command::NEXT;
  if (command == "queue" || command == ".ls")
    return Command::QUEUE;
  if (command == "clear" || command == ".c")
    return Command::CLEAR;
  if (command == "volume" || command == "vol" || command == ".v")
    return Command::VOLUME;
  if (command == "help" || command == ".h" || command == "?")
    return Command::HELP;
  return Command::UNKNOWN;
}

Cli::Cli(std::unique_ptr<Mixer> &mixer) : _mixer(mixer) {}

void Cli::Run() {
  auto *ctx = _mixer->GetCtxRef();
  const auto input = readLine();
  if (!input) {
    _mixer->Shutdown();
    return;
  }
  const string line = *input;
  redrawInput({}, 0);

  const auto args = parseArgs(line);
  if (args.empty()) {
    return;
  }

  switch (StringToCommand(args.front())) {
  case Command::EXIT:
    _mixer->Shutdown();
    break;
  case Command::PLAY: {
    const string query = joinQuery(args);
    if (query.empty()) {
      _mixer->PublishMessage("Usage: play <song, URL, or local file>", true);
      break;
    }
    _mixer->AddToQueue(query);
    break;
  }
  case Command::NEXT:
    _mixer->NextSong();
    break;
  case Command::STOP:
    _mixer->Stop(false, true);
    break;
  case Command::REPEAT:
    _repeatRequested.store(!_repeatRequested.load());
    _mixer->ToggleRepeat(_repeatRequested.load());
    break;
  case Command::PAUSE:
    _mixer->Pause();
    break;
  case Command::RESUME:
    _mixer->Resume();
    break;
  case Command::QUEUE: {
    const auto count = ctx->trackQueue->Size();
    _mixer->PublishMessage(count == 1 ? "1 track is queued."
                                      : std::to_string(count) +
                                            " tracks are queued.");
    break;
  }
  case Command::CLEAR:
    _mixer->ClearQueue();
    _mixer->PublishMessage("Upcoming tracks cleared.");
    break;
  case Command::VOLUME: {
    if (args.size() == 1) {
      _mixer->PublishMessage("Volume is " +
                             std::to_string(_mixer->Volume()) + "%.");
      break;
    }
    if (args.size() != 2) {
      _mixer->PublishMessage("Usage: volume <0-100 | +N | -N>", true);
      break;
    }
    try {
      std::size_t parsed = 0;
      const int value = std::stoi(args[1], &parsed);
      if (parsed != args[1].size()) {
        throw std::invalid_argument("trailing characters");
      }
      const bool relative = args[1].starts_with('+') || args[1].starts_with('-');
      _mixer->SetVolume(relative ? _mixer->Volume() + value : value);
    } catch (const std::exception &) {
      _mixer->PublishMessage("Usage: volume <0-100 | +N | -N>", true);
    }
    break;
  }
  case Command::HELP:
    if (args.size() == 1) {
      _mixer->PublishMessage(
          "play  pause  resume  next  repeat  volume  stop  queue  clear  "
          "exit — use help <command>");
      break;
    }
    if (args.size() != 2) {
      _mixer->PublishMessage("Usage: help [command]", true);
      break;
    }
    if (const auto help = commandHelp(args[1])) {
      _mixer->PublishMessage(*help);
    } else {
      _mixer->PublishMessage(
          "Unknown help topic. Try: help play, help volume, or help stop.",
          true);
    }
    break;
  case Command::UNKNOWN:
    _mixer->PublishMessage("Unknown command. Type 'help' for commands.", true);
    break;
  }
}

std::optional<string> Cli::readLine() {
  if (!isatty(STDIN_FILENO)) {
    redrawInput({}, 0);
    string line;
    if (!std::getline(std::cin, line)) {
      return std::nullopt;
    }
    return line;
  }

  string line;
  std::size_t cursor = 0;
  redrawInput(line, cursor);

  while (_mixer->GetCtxRef()->playbackCtx->gRunning.load()) {
    unsigned char input[4]{};
    const ssize_t count = read(STDIN_FILENO, input, 1);
    if (count < 0) {
      if (errno == EINTR) {
        return std::nullopt;
      }
      continue;
    }
    if (count == 0 || input[0] == 4) {
      return line.empty() ? std::nullopt : std::optional<string>(line);
    }
    if (input[0] == '\r' || input[0] == '\n') {
      return line;
    }

    if (input[0] == 127 || input[0] == 8) {
      if (cursor > 0) {
        const std::size_t previous = previousCharacter(line, cursor);
        line.erase(previous, cursor - previous);
        cursor = previous;
      }
      redrawInput(line, cursor);
      continue;
    }

    if (input[0] == 27) {
      unsigned char sequence[3]{};
      if (read(STDIN_FILENO, sequence, 2) == 2 && sequence[0] == '[') {
        if (sequence[1] == 'D') {
          cursor = previousCharacter(line, cursor);
        } else if (sequence[1] == 'C') {
          cursor = nextCharacter(line, cursor);
        } else if (sequence[1] == 'H') {
          cursor = 0;
        } else if (sequence[1] == 'F') {
          cursor = line.size();
        } else if (sequence[1] == '3' &&
                   read(STDIN_FILENO, sequence + 2, 1) == 1 &&
                   sequence[2] == '~' && cursor < line.size()) {
          line.erase(cursor, nextCharacter(line, cursor) - cursor);
        }
      }
      redrawInput(line, cursor);
      continue;
    }

    std::size_t byteCount = 1;
    if ((input[0] & 0xE0) == 0xC0)
      byteCount = 2;
    else if ((input[0] & 0xF0) == 0xE0)
      byteCount = 3;
    else if ((input[0] & 0xF8) == 0xF0)
      byteCount = 4;

    for (std::size_t index = 1; index < byteCount; ++index) {
      if (read(STDIN_FILENO, input + index, 1) != 1) {
        byteCount = index;
        break;
      }
    }

    if (input[0] >= 32 || byteCount > 1) {
      line.insert(cursor, reinterpret_cast<char *>(input), byteCount);
      cursor += byteCount;
      redrawInput(line, cursor);
    }
  }

  return std::nullopt;
}

void Cli::redrawInput(const string &line, std::size_t cursor) const {
  auto *visualization = _mixer->GetCtxRef()->visualizationCtx;
  {
    std::lock_guard inputLock(visualization->inputMtx);
    visualization->inputLine = line;
    visualization->inputCursor = cursor;
  }

  const int left = visualization->uiLeftColumn.load();
  const int promptRow = visualization->uiTopRow.load() +
                        VisualizationContext::PROMPT_ROW - 1;
  std::lock_guard lock(visualization->coutMtx);
  std::cout << "\033[" << promptRow
            << ';' << left
            << "H\033[2K\033[38;5;81mtermify\033[0m \u203a " << line;
  const std::size_t charactersAfterCursor = characterCount(line, cursor);
  if (charactersAfterCursor > 0) {
    std::cout << "\033[" << charactersAfterCursor << 'D';
  }
  std::cout << std::flush;
}

std::size_t Cli::previousCharacter(const string &line, std::size_t cursor) {
  if (cursor == 0) {
    return 0;
  }
  --cursor;
  while (cursor > 0 &&
         (static_cast<unsigned char>(line[cursor]) & 0xC0) == 0x80) {
    --cursor;
  }
  return cursor;
}

std::size_t Cli::nextCharacter(const string &line, std::size_t cursor) {
  if (cursor >= line.size()) {
    return line.size();
  }
  ++cursor;
  while (cursor < line.size() &&
         (static_cast<unsigned char>(line[cursor]) & 0xC0) == 0x80) {
    ++cursor;
  }
  return cursor;
}

std::size_t Cli::characterCount(const string &line, std::size_t begin) {
  return static_cast<std::size_t>(std::count_if(
      line.begin() + static_cast<std::ptrdiff_t>(begin), line.end(),
      [](unsigned char ch) { return (ch & 0xC0) != 0x80; }));
}

vector<string> Cli::parseArgs(const string &input) const {
  vector<string> result;
  std::istringstream stream(input);
  string token;
  while (stream >> std::quoted(token)) {
    result.push_back(token);
  }
  return result;
}

string Cli::joinQuery(const vector<string> &args) {
  if (args.size() < 2) {
    return {};
  }

  std::ostringstream query;
  for (std::size_t index = 1; index < args.size(); ++index) {
    if (index > 1) {
      query << ' ';
    }
    query << args[index];
  }
  return query.str();
}

} // namespace termify::core
