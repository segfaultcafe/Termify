#include "Cli.hpp"

#include <charconv>
#include <cctype>
#include <limits>

namespace termify::core {

namespace {
std::optional<string> commandHelp(const string &topic) {
  switch (StringToCommand(topic)) {
  case Command::PLAY:
    return "play <song, URL, or local file> — play if idle, otherwise queue "
           "next";
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
  case Command::SEEK:
    return "seek <seconds|M:SS|H:MM:SS> — jump to a position; prefix with + "
           "or - to move relative to the current position";
  case Command::HISTORY:
    return "history — open play history; arrows select, Enter queues a track";
  case Command::PLAYER:
    return "player — return to the player tab";
  case Command::PLAYLISTS:
    return "playlists — open the playlist manager";
  case Command::SEARCH:
    return "search — open YouTube search; enter a query in its search box";
  case Command::QUEUE:
    return "queue [play N|remove N|move N up|down|clear] — inspect or edit "
           "upcoming tracks";
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
  if (command == "seek")
    return Command::SEEK;
  if (command == "history")
    return Command::HISTORY;
  if (command == "player")
    return Command::PLAYER;
  if (command == "playlists" || command == "playlist")
    return Command::PLAYLISTS;
  if (command == "search")
    return Command::SEARCH;
  if (command == "help" || command == ".h" || command == "?")
    return Command::HELP;
  return Command::UNKNOWN;
}

std::optional<SeekArgument> ParseSeekArgument(const string &value) {
  if (value.empty())
    return std::nullopt;
  std::size_t start = 0;
  int sign = 1;
  bool relative = false;
  if (value.front() == '+' || value.front() == '-') {
    relative = true;
    sign = value.front() == '-' ? -1 : 1;
    start = 1;
  }
  if (start == value.size())
    return std::nullopt;

  vector<std::int64_t> parts;
  while (start <= value.size()) {
    const auto separator = value.find(':', start);
    const auto end = separator == string::npos ? value.size() : separator;
    if (end == start || parts.size() == 3)
      return std::nullopt;
    std::int64_t part = 0;
    const auto [parsed, error] =
        std::from_chars(value.data() + start, value.data() + end, part);
    if (error != std::errc{} || parsed != value.data() + end || part < 0)
      return std::nullopt;
    parts.push_back(part);
    if (separator == string::npos)
      break;
    start = separator + 1;
  }
  if (parts.size() > 1) {
    for (std::size_t index = 1; index < parts.size(); ++index) {
      if (parts[index] >= 60)
        return std::nullopt;
    }
  }
  std::int64_t seconds = 0;
  for (const auto part : parts) {
    if (seconds > (std::numeric_limits<std::int64_t>::max() - part) / 60)
      return std::nullopt;
    seconds = seconds * 60 + part;
  }
  return SeekArgument{seconds * sign, relative};
}

Cli::Cli(std::unique_ptr<Mixer> &mixer,
         std::function<void(std::size_t)> switchTab)
    : _mixer(mixer), _switchTab(std::move(switchTab)) {}

void Cli::Execute(const string &line) {
  auto *ctx = _mixer->GetCtxRef();

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
    _mixer->ToggleRepeat(!_mixer->IsRepeatOn());
    break;
  case Command::PAUSE:
    _mixer->Pause();
    break;
  case Command::RESUME:
    _mixer->Resume();
    break;
  case Command::QUEUE: {
    const auto songs = ctx->trackQueue->Snapshot(SongQueue::MAX_SONGS);
    const auto normalized = [](string value) {
      std::transform(value.begin(), value.end(), value.begin(),
                     [](unsigned char ch) { return std::tolower(ch); });
      return value;
    };
    const string action = args.size() > 1 ? normalized(args[1]) : string{};
    const string direction = args.size() > 3 ? normalized(args[3]) : string{};
    const auto selectedSong = [&](const string &position)
        -> std::optional<QueuedSong> {
      std::size_t parsed = 0;
      const auto [end, error] = std::from_chars(
          position.data(), position.data() + position.size(), parsed);
      if (error != std::errc{} || end != position.data() + position.size() ||
          parsed == 0 || parsed > songs.size())
        return std::nullopt;
      return songs[parsed - 1];
    };
    if (args.size() == 2 && action == "clear") {
      _mixer->ClearQueue();
      _mixer->PublishMessage("Upcoming tracks cleared.");
      break;
    }
    if (args.size() == 3 && (action == "play" || action == "remove")) {
      const auto song = selectedSong(args[2]);
      if (!song) {
        _mixer->PublishMessage("Choose a valid queue position.", true);
      } else if (action == "play") {
        _mixer->PlayQueuedNow(song->id);
      } else {
        _mixer->RemoveQueued(song->id);
      }
      break;
    }
    if (args.size() == 4 && action == "move" &&
        (direction == "up" || direction == "down")) {
      const auto song = selectedSong(args[2]);
      if (!song)
        _mixer->PublishMessage("Choose a valid queue position.", true);
      else
        _mixer->MoveQueued(song->id, direction == "up" ? -1 : 1);
      break;
    }
    if (args.size() != 1) {
      _mixer->PublishMessage(
          "Usage: queue [play N | remove N | move N up|down | clear]",
          true);
      break;
    }
    const auto count = ctx->trackQueue->Size();
    if (_mixer->IsPlaylistLooping()) {
      _mixer->PublishMessage("The playlist queue loops indefinitely (" +
                             std::to_string(count) + " tracks buffered).");
    } else {
      _mixer->PublishMessage(
          count == 1 ? "1 track is queued."
                     : std::to_string(count) + " tracks are queued.");
    }
    break;
  }
  case Command::CLEAR:
    _mixer->ClearQueue();
    _mixer->PublishMessage("Upcoming tracks cleared.");
    break;
  case Command::VOLUME: {
    if (args.size() == 1) {
      _mixer->PublishMessage("Volume is " + std::to_string(_mixer->Volume()) +
                             "%.");
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
      const bool relative =
          args[1].starts_with('+') || args[1].starts_with('-');
      _mixer->SetVolume(relative ? _mixer->Volume() + value : value);
    } catch (const std::exception &) {
      _mixer->PublishMessage("Usage: volume <0-100 | +N | -N>", true);
    }
    break;
  }
  case Command::SEEK: {
    if (args.size() != 2) {
      _mixer->PublishMessage("Usage: seek <seconds | M:SS | H:MM:SS>", true);
      break;
    }
    const auto seek = ParseSeekArgument(args[1]);
    if (!seek) {
      _mixer->PublishMessage("Usage: seek <seconds | M:SS | H:MM:SS>", true);
      break;
    }
    if (seek->relative)
      _mixer->SeekRelativeSeconds(seek->seconds);
    else
      _mixer->SeekToSeconds(seek->seconds);
    break;
  }
  case Command::HELP:
    if (args.size() == 1) {
      _mixer->PublishMessage("play  pause  resume  next  repeat  volume  seek  "
                             "history  playlists  search  player  stop  queue  "
                             "clear  exit — use help <command>");
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
  case Command::HISTORY:
  case Command::PLAYER:
  case Command::PLAYLISTS:
  case Command::SEARCH:
    if (args.size() != 1) {
      _mixer->PublishMessage(
          "Usage: history | playlists | search | player (no arguments)",
          true);
    } else if (_switchTab) {
      const auto command = StringToCommand(args.front());
      _switchTab(command == Command::HISTORY     ? 1
                 : command == Command::PLAYLISTS ? 2
                 : command == Command::SEARCH    ? 3
                                                 : 0);
    }
    break;
  case Command::UNKNOWN:
    _mixer->PublishMessage("Unknown command. Type 'help' for commands.", true);
    break;
  }
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
