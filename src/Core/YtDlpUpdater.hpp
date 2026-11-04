#ifndef TERMIFY_CORE_YT_DLP_UPDATER_HPP
#define TERMIFY_CORE_YT_DLP_UPDATER_HPP

#include "../Platform/Services.hpp"
#include <compare>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

namespace termify::core {

struct YtDlpVersion {
  unsigned year{0};
  unsigned month{0};
  unsigned day{0};
  unsigned revision{0};

  static YtDlpVersion Parse(std::string_view value);
  std::string String() const;
  auto operator<=>(const YtDlpVersion &) const = default;
};

enum class YtDlpUpdateState { UpToDate, Available, DevelopmentBuild };

struct YtDlpUpdateCheckResult {
  YtDlpUpdateState state{YtDlpUpdateState::UpToDate};
  YtDlpVersion current;
  YtDlpVersion latest;
  std::string releaseUrl;
};

struct PreparedYtDlpUpdate {
  YtDlpVersion version;
  std::string artifactName;
  std::filesystem::path path;
  std::filesystem::path manifestPath;
  platform::ReleasePlatform platform{platform::ReleasePlatform::Linux};
  platform::ReleaseArchitecture architecture{
      platform::ReleaseArchitecture::Unsupported};
};

class YtDlpUpdater {
public:
  YtDlpUpdater(platform::ProcessRunner &processes, platform::FileSystem &files);

  YtDlpUpdateCheckResult
  Check(const platform::UpdateEnvironment &environment) const;
  PreparedYtDlpUpdate
  Prepare(const YtDlpUpdateCheckResult &release,
          const platform::UpdateEnvironment &environment) const;
  void Install(const PreparedYtDlpUpdate &update,
               const platform::UpdateEnvironment &environment) const;
  void Rollback(const platform::UpdateEnvironment &environment) const;

  static YtDlpVersion LatestFromRefs(std::string_view response);
  static std::string
  ArtifactName(const YtDlpVersion &version,
               const platform::UpdateEnvironment &environment);
  static std::string Format(const YtDlpUpdateCheckResult &result);

private:
  struct RollbackState {
    std::string backup;
    bool committed{false};
  };

  std::string FetchReleaseRefs() const;
  YtDlpVersion
  ExecutableVersion(const std::filesystem::path &executable,
                    const std::filesystem::path &temporaryDirectory) const;
  void Download(std::string_view url, const std::filesystem::path &output,
                std::uint64_t maximumBytes) const;
  void VerifyAttestation(const std::filesystem::path &subject,
                         const std::filesystem::path &temporaryDirectory) const;
  std::filesystem::path DataDirectory() const;
  std::filesystem::path RollbackDirectory() const;
  std::shared_ptr<platform::FileLease>
  AcquireLock(const std::filesystem::path &directory) const;
  std::optional<RollbackState>
  LoadState(const std::filesystem::path &directory) const;
  void SaveState(const std::filesystem::path &directory,
                 const RollbackState &state) const;
  std::filesystem::path Snapshot(const std::filesystem::path &source) const;
  void RemoveBackup(const RollbackState &state) const;
  static void RequireUnmanaged(const platform::UpdateEnvironment &environment);

  platform::ProcessRunner &_processes;
  platform::FileSystem &_files;
};

} // namespace termify::core

#endif // TERMIFY_CORE_YT_DLP_UPDATER_HPP
