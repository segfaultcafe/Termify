#include "Cli.hpp"

#include <cctype>

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
  case Command::HISTORY:
    return "history — open play history; arrows select, Enter queues a track";
  case Command::PLAYER:
    return "player — return to the player tab";
  case Command::PLAYLISTS:
    return "playlists — open the playlist manager";
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
  if (command == "history")
    return Command::HISTORY;
  if (command == "player")
    return Command::PLAYER;
  if (command == "playlists" || command == "playlist")
    return Command::PLAYLISTS;
  if (command == "help" || command == ".h" || command == "?")
    return Command::HELP;
  return Command::UNKNOWN;
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
    const auto count = ctx->trackQueue->Size();
    _mixer->PublishMessage(count == 1
                               ? "1 track is queued."
                               : std::to_string(count) + " tracks are queued.");
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
  case Command::HELP:
    if (args.size() == 1) {
      _mixer->PublishMessage("play  pause  resume  next  repeat  volume  "
                             "history  playlists  player  stop  queue  clear  "
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
  case Command::HISTORY:
  case Command::PLAYER:
  case Command::PLAYLISTS:
    if (args.size() != 1) {
      _mixer->PublishMessage(
          "Usage: history | playlists | player (no arguments)", true);
    } else if (_switchTab) {
      const auto command = StringToCommand(args.front());
      _switchTab(command == Command::HISTORY     ? 1
                 : command == Command::PLAYLISTS ? 2
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
