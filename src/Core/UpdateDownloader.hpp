#ifndef TERMIFY_CORE_UPDATE_DOWNLOADER_HPP
#define TERMIFY_CORE_UPDATE_DOWNLOADER_HPP

#include "UpdateChecker.hpp"
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>

namespace termify::core {

struct PreparedUpdate {
  Version version;
  std::string artifactName;
  std::filesystem::path path;
  std::filesystem::path manifestPath;
  platform::InstallationKind installation{platform::InstallationKind::UserLocal};
};

class UpdateDownloader {
public:
  UpdateDownloader(platform::ProcessRunner &processes,
                   platform::FileSystem &files);

  PreparedUpdate Prepare(const UpdateCheckResult &release,
                         const platform::UpdateEnvironment &environment) const;

  static std::string
  ArtifactName(const Version &version,
               const platform::UpdateEnvironment &environment);
  static std::string ExpectedChecksum(std::string_view manifest,
                                      std::string_view artifactName);

private:
  void Download(std::string_view url, const std::filesystem::path &output,
                std::uint64_t maximumBytes) const;
  void VerifyAttestation(const std::filesystem::path &subject,
                         const Version &version,
                         const std::filesystem::path &temporaryDirectory) const;

  platform::ProcessRunner &_processes;
  platform::FileSystem &_files;
};

} // namespace termify::core

#endif // TERMIFY_CORE_UPDATE_DOWNLOADER_HPP
