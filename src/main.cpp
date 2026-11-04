#include "App/Application.hpp"
#include "Core/UpdateChecker.hpp"
#include "Core/UpdateDownloader.hpp"
#include "Core/UpdateInstaller.hpp"
#include "Platform/Services.hpp"
#include <cstdlib>
#include <exception>
#include <iostream>

namespace {
void printHelp() {
  std::cout
      << "Usage: termify [OPTION]\n"
      << "       termify update [--check|--download|--rollback]\n"
      << "A terminal audio player for local files and online searches.\n\n"
      << "Options:\n"
      << "  -h, --help       Show this help and exit\n"
      << "  -V, --version    Show the version and exit\n"
      << "  update             Verify and install a stable update\n"
      << "  update --check     Check for a newer stable release\n"
      << "  update --download  Download and verify a stable update\n"
      << "  update --rollback  Restore the version replaced by update\n\n"
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
    const bool updateCommand =
        option == "update" &&
        (argc == 2 || (argc == 3 && (std::string(argv[2]) == "--check" ||
                                     std::string(argv[2]) == "--download" ||
                                     std::string(argv[2]) == "--rollback")));
    if (updateCommand) {
      try {
        auto &platform = termify::platform::CurrentPlatform();
        const auto environment = platform.UpdateEnvironmentInfo();
        if (argc == 3 && std::string(argv[2]) == "--rollback") {
          termify::core::UpdateInstaller installer(platform.Processes(),
                                                   platform.Files());
          installer.Rollback(environment);
          std::cout << "Restored the previous Termify version.\n";
          return EXIT_SUCCESS;
        }
        termify::core::UpdateChecker checker(platform.Processes(),
                                             platform.Files());
        const auto result = checker.Check(TERMIFY_VERSION);
        if ((argc == 3 && std::string(argv[2]) == "--check") ||
            result.state != termify::core::UpdateState::Available) {
          std::cout << termify::core::UpdateChecker::Format(result);
          return EXIT_SUCCESS;
        }
        termify::core::UpdateDownloader downloader(platform.Processes(),
                                                   platform.Files());
        const auto prepared = downloader.Prepare(result, environment);
        if (argc == 3 && std::string(argv[2]) == "--download") {
          std::cout << "Verified Termify v" << prepared.version.String()
                    << " update at " << prepared.path << "\n"
                    << "No files were installed.\n";
          return EXIT_SUCCESS;
        }
        termify::core::UpdateInstaller installer(platform.Processes(),
                                                 platform.Files());
        installer.Install(prepared, environment);
        std::cout << "Updated Termify to v" << prepared.version.String()
                  << ". Use 'termify update --rollback' to restore the "
                     "previous version.\n";
        return EXIT_SUCCESS;
      } catch (const std::exception &error) {
        std::cerr << "termify: " << error.what() << '\n';
        return EXIT_FAILURE;
      }
    }
    if (option == "update") {
      std::cerr << "termify: usage: termify update "
                   "[--check|--download|--rollback]\n";
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
