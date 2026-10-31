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
  HELP,
  UNKNOWN
};

Command StringToCommand(string command);

class Cli {
public:
  explicit Cli(std::unique_ptr<Mixer> &mixer,
                std::function<void(bool)> switchTab = {});
  void Execute(const string &line);

private:
  vector<string> parseArgs(const string &input) const;
  static string joinQuery(const vector<string> &args);

  atomic_bool _repeatRequested{false};
  std::unique_ptr<Mixer> &_mixer;
  std::function<void(bool)> _switchTab;
};

} // namespace termify::core

#endif // _TERMIFY_CORE_CLI_HPP
