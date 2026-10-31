#ifndef _TERMIFY_CORE_CLI_HPP
#define _TERMIFY_CORE_CLI_HPP

#include "Mixer.hpp"
#include <functional>

namespace termify::core {

enum class Command {
  EXIT,
  PLAY,
  STOP,
  REPEAT,
  PAUSE,
  RESUME,
  NEXT,
  QUEUE,
  CLEAR,
  VOLUME,
  HISTORY,
  PLAYER,
  PLAYLISTS,
  HELP,
  UNKNOWN
};

Command StringToCommand(string command);

class Cli {
public:
  explicit Cli(std::unique_ptr<Mixer> &mixer,
               std::function<void(std::size_t)> switchTab = {});
  void Execute(const string &line);

private:
  vector<string> parseArgs(const string &input) const;
  static string joinQuery(const vector<string> &args);

  std::unique_ptr<Mixer> &_mixer;
  std::function<void(std::size_t)> _switchTab;
};

} // namespace termify::core

#endif // _TERMIFY_CORE_CLI_HPP
