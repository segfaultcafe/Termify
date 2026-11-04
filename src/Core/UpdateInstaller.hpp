#ifndef TERMIFY_CORE_UPDATE_INSTALLER_HPP
#define TERMIFY_CORE_UPDATE_INSTALLER_HPP

#include "UpdateDownloader.hpp"
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

namespace termify::core {

class UpdateInstaller {
public:
  UpdateInstaller(platform::ProcessRunner &processes,
                  platform::FileSystem &files);

  void Install(const PreparedUpdate &update,
               const platform::UpdateEnvironment &environment) const;
  void Rollback(const platform::UpdateEnvironment &environment) const;

private:
  struct Targets {
    platform::InstallationKind installation;
    std::filesystem::path termify;
    std::filesystem::path downloader;
  };

  struct RollbackState {
    Targets targets;
    std::string termifyBackup;
    std::string downloaderBackup;
    bool committed{false};
  };

  Targets ResolveTargets(const platform::UpdateEnvironment &environment) const;
  std::filesystem::path RollbackDirectory() const;
  std::filesystem::path
  StatePath(const std::filesystem::path &termifyTarget) const;
  std::filesystem::path Snapshot(const std::filesystem::path &source,
                                 std::string_view prefix) const;
  std::shared_ptr<platform::FileLease>
  AcquireLock(const std::filesystem::path &termifyTarget) const;
  std::optional<RollbackState>
  LoadState(const std::filesystem::path &termifyTarget) const;
  void SaveState(const RollbackState &state) const;
  void RemoveBackups(const RollbackState &state) const;

  platform::ProcessRunner &_processes;
  platform::FileSystem &_files;
};

} // namespace termify::core

#endif // TERMIFY_CORE_UPDATE_INSTALLER_HPP
