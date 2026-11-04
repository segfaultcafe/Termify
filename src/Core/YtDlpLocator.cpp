#include "YtDlpLocator.hpp"

#include <cstdlib>
#include <vector>

namespace termify::core {

std::filesystem::path ManagedYtDlpPath(platform::FileSystem &files) {
  return files.UserDataDirectory("termify") / "yt-dlp" / "yt-dlp";
}

std::filesystem::path
FindYtDlpExecutable(platform::FileSystem &files,
                    const platform::UpdateEnvironment &environment) {
  std::vector<std::filesystem::path> preferred;
  if (const char *overridePath = std::getenv("TERMIFY_YTDLP");
      overridePath && *overridePath != '\0')
    preferred.emplace_back(overridePath);

  // Package-managed installations must not silently escape their package
  // manager by loading a downloader previously installed by another Termify
  // distribution (for example an AppImage).
  if (environment.installation != platform::InstallationKind::PackageManaged)
    preferred.push_back(ManagedYtDlpPath(files));

  const auto executableDirectory = files.ExecutableDirectory();
  preferred.push_back(executableDirectory / "yt-dlp");
  preferred.push_back(executableDirectory.parent_path() / "lib/termify/yt-dlp");
  preferred.push_back(executableDirectory / ".local/bin/yt-dlp");
  return files.FindExecutable("yt-dlp", preferred)
      .value_or(std::filesystem::path{});
}

} // namespace termify::core
