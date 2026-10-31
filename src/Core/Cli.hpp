#ifndef _TERMIFY_CORE_CLI_HPP
#define _TERMIFY_CORE_CLI_HPP

#include "Mixer.hpp"
#include <optional>

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
  HELP,
  UNKNOWN
};

Command StringToCommand(string command);

class Cli {
public:
  explicit Cli(std::unique_ptr<Mixer> &mixer);
  void Run();

private:
  vector<string> parseArgs(const string &input) const;
  static string joinQuery(const vector<string> &args);
  std::optional<string> readLine();
  void redrawInput(const string &line, std::size_t cursor) const;
  static std::size_t previousCharacter(const string &line,
                                       std::size_t cursor);
  static std::size_t nextCharacter(const string &line, std::size_t cursor);
  static std::size_t characterCount(const string &line,
                                    std::size_t begin = 0);

  atomic_bool _repeatRequested{false};
  std::unique_ptr<Mixer> &_mixer;
};

} // namespace termify::core

#endif // _TERMIFY_CORE_CLI_HPP
