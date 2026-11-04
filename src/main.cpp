#include "App/Application.hpp"
#include "Core/UpdateChecker.hpp"
#include "Platform/Services.hpp"
#include <cstdlib>
#include <exception>
#include <iostream>

namespace {
void printHelp() {
  std::cout
      << "Usage: termify [OPTION]\n"
      << "       termify update --check\n"
      << "A terminal audio player for local files and online searches.\n\n"
      << "Options:\n"
      << "  -h, --help       Show this help and exit\n"
      << "  -V, --version    Show the version and exit\n"
      << "  update --check   Check for a newer stable release\n\n"
      << "Interactive commands:\n"
      << "  play <song|URL|file>  pause  resume  next  repeat\n"
      << "  volume <0-100|+N|-N>  seek <seconds|M:SS>\n"
      << "  history  playlists  search  player\n"
      << "  queue [play N|remove N|move N up|down|clear]\n"
      << "  clear  stop  help  exit\n";
}
} // namespace

int main(int argc, char **argv) {
  if (argc > 1) {
    const string option = argv[1];
    if (argc == 2 && (option == "--help" || option == "-h")) {
      printHelp();
      return EXIT_SUCCESS;
    }
    if (argc == 2 && (option == "--version" || option == "-V")) {
      std::cout << "termify " << TERMIFY_VERSION << '\n';
      return EXIT_SUCCESS;
    }
    if (argc == 3 && option == "update" &&
        std::string(argv[2]) == "--check") {
      try {
        auto &platform = termify::platform::CurrentPlatform();
        termify::core::UpdateChecker checker(platform.Processes(),
                                             platform.Files());
        std::cout << termify::core::UpdateChecker::Format(
            checker.Check(TERMIFY_VERSION));
        return EXIT_SUCCESS;
      } catch (const std::exception &error) {
        std::cerr << "termify: " << error.what() << '\n';
        return EXIT_FAILURE;
      }
    }
    if (option == "update") {
      std::cerr << "termify: usage: termify update --check\n";
      return EXIT_FAILURE;
    }
    std::cerr << "termify: unknown option '" << option
              << "'\nTry 'termify --help' for more information.\n";
    return EXIT_FAILURE;
  }

  termify::platform::CurrentPlatform().InstallInterruptHandler();

  try {
    termify::app::Application app;
    app.Start();
    return EXIT_SUCCESS;
  } catch (const std::exception &error) {
    std::cerr << "termify: " << error.what() << '\n';
    return EXIT_FAILURE;
  }
}
