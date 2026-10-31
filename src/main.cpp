#include "App/Application.hpp"
#include <csignal>
#include <cstdlib>
#include <exception>
#include <iostream>

namespace {
void handleInterrupt(int) {}

void printHelp() {
  std::cout
      << "Usage: termify [OPTION]\n"
      << "A terminal audio player for local files and online searches.\n\n"
      << "Options:\n"
      << "  -h, --help       Show this help and exit\n"
      << "  -V, --version    Show the version and exit\n\n"
      << "Interactive commands:\n"
      << "  play <song|URL|file>  pause  resume  next  repeat\n"
      << "  volume <0-100|+N|-N>  history  player  queue  clear  stop  help  exit\n";
}
}

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
    std::cerr << "termify: unknown option '" << option
              << "'\nTry 'termify --help' for more information.\n";
    return EXIT_FAILURE;
  }

  struct sigaction action {};
  action.sa_handler = handleInterrupt;
  sigemptyset(&action.sa_mask);
  sigaction(SIGINT, &action, nullptr);

  try {
    termify::app::Application app;
    app.Start();
    return EXIT_SUCCESS;
  } catch (const std::exception &error) {
    std::cerr << "termify: " << error.what() << '\n';
    return EXIT_FAILURE;
  }
}
