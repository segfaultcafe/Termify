#include "UpdateInstaller.hpp"

#include "Sha256.hpp"

#include <chrono>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <system_error>
#include <utility>

namespace termify::core {
namespace {

constexpr std::string_view STATE_HEADER = "TERMIFY_ROLLBACK_V1";
constexpr std::string_view ARCHIVE_SUFFIX = ".tar.gz";

class TemporaryDirectory {
public:
  explicit TemporaryDirectory(std::filesystem::path value)
      : path(std::move(value)) {}
  ~TemporaryDirectory() {
    std::error_code error;
    std::filesystem::remove_all(path, error);
  }

  TemporaryDirectory(const TemporaryDirectory &) = delete;
  TemporaryDirectory &operator=(const TemporaryDirectory &) = delete;

  std::filesystem::path path;
};

bool isRegularFile(const std::filesystem::path &path) {
  std::error_code error;
  const auto status = std::filesystem::symlink_status(path, error);
  return !error && std::filesystem::is_regular_file(status) &&
         std::filesystem::file_size(path, error) > 0 && !error;
}

void requireSafePath(const std::filesystem::path &path,
                     std::string_view description) {
  const auto value = path.string();
  if (!path.is_absolute() || value.find_first_of("\r\n") != std::string::npos)
    throw std::runtime_error(std::string(description) + " path is not safe");
}

std::string installationName(platform::InstallationKind installation) {
  switch (installation) {
  case platform::InstallationKind::AppImage:
    return "appimage";
  case platform::InstallationKind::UserLocal:
    return "user-local";
  case platform::InstallationKind::PackageManaged:
    break;
  }
  throw std::runtime_error(
      "This Termify installation is managed by a package manager; use that "
      "package manager to update");
}

platform::InstallationKind parseInstallation(std::string_view value) {
  if (value == "appimage")
    return platform::InstallationKind::AppImage;
  if (value == "user-local")
    return platform::InstallationKind::UserLocal;
  throw std::runtime_error("The Termify rollback state is invalid");
}

void restore(platform::FileSystem &files, const std::filesystem::path &backup,
             const std::filesystem::path &target) {
  files.ReplaceFileAtomically(backup, target, true);
}

} // namespace

UpdateInstaller::UpdateInstaller(platform::ProcessRunner &processes,
                                 platform::FileSystem &files)
    : _processes(processes), _files(files) {}

UpdateInstaller::Targets UpdateInstaller::ResolveTargets(
    const platform::UpdateEnvironment &environment) const {
  if (environment.installation == platform::InstallationKind::PackageManaged)
    installationName(environment.installation);
  requireSafePath(environment.executable, "The Termify executable");
  if (!isRegularFile(environment.executable))
    throw std::runtime_error("The installed Termify executable is not safe");

  Targets targets{
      environment.installation, environment.executable.lexically_normal(), {}};
  if (environment.installation == platform::InstallationKind::AppImage)
    return targets;

  if (targets.termify.filename() != "termify" ||
      targets.termify.parent_path().filename() != "bin")
    throw std::runtime_error(
        "User-local updates require Termify at PREFIX/bin/termify");
  const auto prefix = targets.termify.parent_path().parent_path();
  const auto privateDownloader = prefix / "lib/termify/yt-dlp";
  const auto fallbackDownloader = prefix / "bin/yt-dlp";
  if (isRegularFile(privateDownloader))
    targets.downloader = privateDownloader;
  else if (isRegularFile(fallbackDownloader))
    targets.downloader = fallbackDownloader;
  else
    throw std::runtime_error(
        "The installed Termify downloader could not be located");
  return targets;
}

std::filesystem::path UpdateInstaller::RollbackDirectory() const {
  const auto directory =
      _files.UserCacheDirectory("termify") / "updates" / "rollback";
  std::error_code error;
  if (std::filesystem::is_symlink(directory, error))
    throw std::runtime_error("The Termify rollback directory is not safe");
  error.clear();
  std::filesystem::create_directories(directory, error);
  if (error)
    throw std::runtime_error("Could not create the Termify rollback directory");
  std::filesystem::permissions(directory, std::filesystem::perms::owner_all,
                               std::filesystem::perm_options::replace, error);
  if (error)
    throw std::runtime_error("Could not secure the Termify rollback directory");
  return directory;
}

std::filesystem::path
UpdateInstaller::StatePath(const std::filesystem::path &termifyTarget) const {
  return RollbackDirectory().parent_path() /
         ("rollback-state-" + Sha256(termifyTarget.string()).substr(0, 16));
}

std::filesystem::path
UpdateInstaller::Snapshot(const std::filesystem::path &source,
                          std::string_view prefix) const {
  auto temporary =
      _files.CreateLockedFile(RollbackDirectory(), std::string(prefix));
  _files.ReplaceFileAtomically(source, temporary.path, false);
  return temporary.path;
}

std::shared_ptr<platform::FileLease>
UpdateInstaller::AcquireLock(const std::filesystem::path &termifyTarget) const {
  const auto path =
      RollbackDirectory() /
      ("update-" + Sha256(termifyTarget.string()).substr(0, 16) + ".lock");
  std::error_code error;
  if (std::filesystem::is_symlink(path, error))
    throw std::runtime_error("The Termify update lock is not safe");
  {
    std::ofstream output(path, std::ios::app);
    if (!output)
      throw std::runtime_error("Could not create the Termify update lock");
  }
  std::filesystem::permissions(path,
                               std::filesystem::perms::owner_read |
                                   std::filesystem::perms::owner_write,
                               std::filesystem::perm_options::replace, error);
  if (error)
    throw std::runtime_error("Could not secure the Termify update lock");
  auto lease = _files.TryLock(path, platform::FileLockMode::Exclusive);
  if (!lease)
    throw std::runtime_error(
        "Another update is already changing this Termify installation");
  return lease;
}

std::optional<UpdateInstaller::RollbackState>
UpdateInstaller::LoadState(const std::filesystem::path &termifyTarget) const {
  const auto path = StatePath(termifyTarget);
  std::error_code error;
  if (!std::filesystem::exists(path, error))
    return std::nullopt;
  if (!isRegularFile(path) || std::filesystem::file_size(path, error) > 16384)
    throw std::runtime_error("The Termify rollback state is invalid");

  std::ifstream input(path);
  std::string header;
  std::string phase;
  std::string installation;
  std::string termifyTargetLine;
  std::string termifyBackup;
  std::string downloaderTarget;
  std::string downloaderBackup;
  std::string extra;
  if (!std::getline(input, header) || !std::getline(input, phase) ||
      !std::getline(input, installation) ||
      !std::getline(input, termifyTargetLine) ||
      !std::getline(input, termifyBackup) ||
      !std::getline(input, downloaderTarget) ||
      !std::getline(input, downloaderBackup) || std::getline(input, extra) ||
      header != STATE_HEADER || (phase != "pending" && phase != "ready"))
    throw std::runtime_error("The Termify rollback state is invalid");

  const auto kind = parseInstallation(installation);
  requireSafePath(termifyTargetLine, "The rollback target");
  if (termifyBackup.empty() ||
      std::filesystem::path(termifyBackup).filename() != termifyBackup)
    throw std::runtime_error("The Termify rollback state is invalid");
  if (kind == platform::InstallationKind::AppImage) {
    if (!downloaderTarget.empty() || !downloaderBackup.empty())
      throw std::runtime_error("The Termify rollback state is invalid");
  } else {
    requireSafePath(downloaderTarget, "The downloader rollback target");
    if (downloaderBackup.empty() ||
        std::filesystem::path(downloaderBackup).filename() != downloaderBackup)
      throw std::runtime_error("The Termify rollback state is invalid");
  }
  return RollbackState{{kind, termifyTargetLine, downloaderTarget},
                       termifyBackup,
                       downloaderBackup,
                       phase == "ready"};
}

void UpdateInstaller::SaveState(const RollbackState &state) const {
  const auto path = StatePath(state.targets.termify);
  auto temporary =
      _files.CreateLockedFile(path.parent_path(), "rollback-state.part");
  {
    std::ofstream output(temporary.path, std::ios::trunc);
    if (!output)
      throw std::runtime_error("Could not write the Termify rollback state");
    output << STATE_HEADER << '\n'
           << (state.committed ? "ready" : "pending") << '\n'
           << installationName(state.targets.installation) << '\n'
           << state.targets.termify.string() << '\n'
           << state.termifyBackup << '\n'
           << state.targets.downloader.string() << '\n'
           << state.downloaderBackup << '\n';
    if (!output)
      throw std::runtime_error("Could not write the Termify rollback state");
  }
  try {
    _files.ReplaceFileAtomically(temporary.path, path, false);
  } catch (...) {
    std::error_code error;
    std::filesystem::remove(temporary.path, error);
    throw;
  }
  std::error_code error;
  std::filesystem::remove(temporary.path, error);
}

void UpdateInstaller::RemoveBackups(const RollbackState &state) const {
  const auto directory = RollbackDirectory();
  std::error_code error;
  std::filesystem::remove(directory / state.termifyBackup, error);
  if (!state.downloaderBackup.empty()) {
    error.clear();
    std::filesystem::remove(directory / state.downloaderBackup, error);
  }
}

void UpdateInstaller::Install(
    const PreparedUpdate &update,
    const platform::UpdateEnvironment &environment) const {
  const auto targets = ResolveTargets(environment);
  const auto updateLock = AcquireLock(targets.termify);
  if (update.installation != environment.installation)
    throw std::runtime_error(
        "The verified update does not match this installation type");
  if (!isRegularFile(update.path))
    throw std::runtime_error("The verified update artifact is missing");

  std::filesystem::path termifySource;
  std::filesystem::path downloaderSource;
  std::optional<TemporaryDirectory> extraction;
  if (targets.installation == platform::InstallationKind::AppImage) {
    if (!update.artifactName.ends_with(".AppImage"))
      throw std::runtime_error("The verified update is not an AppImage");
    termifySource = update.path;
  } else {
    if (!update.artifactName.ends_with(ARCHIVE_SUFFIX) ||
        std::filesystem::path(update.artifactName).filename() !=
            update.artifactName)
      throw std::runtime_error("The verified update is not a release archive");
    const auto tar = _files.FindExecutable("tar", {});
    if (!tar)
      throw std::runtime_error("tar is required to install this update");
    extraction.emplace(_files.CreatePrivateDirectory(
        RollbackDirectory().parent_path(), "extract"));
    const auto result = _processes.Run(
        {.executable = *tar,
         .arguments = {"-xzf", update.path.string(), "-C",
                       extraction->path.string(), "--no-same-owner"},
         .standardOutput = std::nullopt,
         .standardError = std::nullopt,
         .discardStandardError = true},
        nullptr, std::chrono::seconds(120));
    if (!result.Succeeded())
      throw std::runtime_error("Could not extract the verified update");
    const auto stem = update.artifactName.substr(0, update.artifactName.size() -
                                                        ARCHIVE_SUFFIX.size());
    termifySource = extraction->path / stem / "termify";
    downloaderSource = extraction->path / stem / "yt-dlp";
    if (!isRegularFile(termifySource) || !isRegularFile(downloaderSource))
      throw std::runtime_error("The verified update archive is incomplete");
  }

  const auto previousState = LoadState(targets.termify);
  if (previousState && !previousState->committed)
    throw std::runtime_error(
        "A previous update was interrupted; run 'termify update --rollback' "
        "before trying again");
  const auto termifyBackup = Snapshot(targets.termify, "termify.previous");
  std::filesystem::path downloaderBackup;
  try {
    if (!targets.downloader.empty())
      downloaderBackup = Snapshot(targets.downloader, "yt-dlp.previous");
  } catch (...) {
    std::error_code error;
    std::filesystem::remove(termifyBackup, error);
    throw;
  }

  const RollbackState nextState{targets, termifyBackup.filename().string(),
                                downloaderBackup.empty()
                                    ? std::string()
                                    : downloaderBackup.filename().string(),
                                false};
  try {
    // Publish the recovery record before replacing either executable. If the
    // process or machine stops mid-transaction, --rollback can still restore
    // the complete pre-update pair.
    SaveState(nextState);
  } catch (...) {
    std::error_code error;
    std::filesystem::remove(termifyBackup, error);
    if (!downloaderBackup.empty()) {
      error.clear();
      std::filesystem::remove(downloaderBackup, error);
    }
    throw;
  }

  bool downloaderReplaced = false;
  bool termifyReplaced = false;
  try {
    if (!targets.downloader.empty()) {
      _files.ReplaceFileAtomically(downloaderSource, targets.downloader, true);
      downloaderReplaced = true;
    }
    _files.ReplaceFileAtomically(termifySource, targets.termify, true);
    termifyReplaced = true;
    auto committedState = nextState;
    committedState.committed = true;
    SaveState(committedState);
  } catch (...) {
    bool restored = true;
    try {
      if (termifyReplaced)
        restore(_files, termifyBackup, targets.termify);
      if (downloaderReplaced)
        restore(_files, downloaderBackup, targets.downloader);
    } catch (...) {
      restored = false;
    }
    if (!restored)
      throw std::runtime_error(
          "Update installation failed and automatic restoration also failed");

    try {
      if (previousState) {
        SaveState(*previousState);
      } else {
        std::error_code error;
        std::filesystem::remove(StatePath(targets.termify), error);
        if (error)
          throw std::runtime_error("Could not clear the update transaction");
      }
    } catch (...) {
      throw std::runtime_error(
          "Update installation failed; the previous version was restored, "
          "but recovery metadata could not be reset");
    }
    std::error_code error;
    std::filesystem::remove(termifyBackup, error);
    if (!downloaderBackup.empty()) {
      error.clear();
      std::filesystem::remove(downloaderBackup, error);
    }
    throw std::runtime_error(
        "Update installation failed; the previous version was restored");
  }

  if (previousState)
    RemoveBackups(*previousState);
}

void UpdateInstaller::Rollback(
    const platform::UpdateEnvironment &environment) const {
  const auto targets = ResolveTargets(environment);
  const auto updateLock = AcquireLock(targets.termify);
  const auto state = LoadState(targets.termify);
  if (!state)
    throw std::runtime_error("There is no Termify update to roll back");
  if (state->targets.installation != targets.installation ||
      state->targets.termify.lexically_normal() != targets.termify ||
      state->targets.downloader.lexically_normal() != targets.downloader)
    throw std::runtime_error(
        "The rollback belongs to a different Termify installation");

  const auto directory = RollbackDirectory();
  const auto termifyBackup = directory / state->termifyBackup;
  const auto downloaderBackup = state->downloaderBackup.empty()
                                    ? std::filesystem::path()
                                    : directory / state->downloaderBackup;
  if (!isRegularFile(termifyBackup) ||
      (!downloaderBackup.empty() && !isRegularFile(downloaderBackup)))
    throw std::runtime_error("The previous Termify version is unavailable");

  const auto currentTermify = Snapshot(targets.termify, "termify.current");
  std::filesystem::path currentDownloader;
  try {
    if (!targets.downloader.empty())
      currentDownloader = Snapshot(targets.downloader, "yt-dlp.current");
  } catch (...) {
    std::error_code error;
    std::filesystem::remove(currentTermify, error);
    throw;
  }

  bool downloaderReplaced = false;
  bool termifyReplaced = false;
  try {
    if (!targets.downloader.empty()) {
      restore(_files, downloaderBackup, targets.downloader);
      downloaderReplaced = true;
    }
    restore(_files, termifyBackup, targets.termify);
    termifyReplaced = true;
  } catch (...) {
    bool restored = true;
    try {
      if (termifyReplaced)
        restore(_files, currentTermify, targets.termify);
      if (downloaderReplaced)
        restore(_files, currentDownloader, targets.downloader);
    } catch (...) {
      restored = false;
    }
    if (!restored)
      throw std::runtime_error(
          "Rollback failed and the installed version could not be restored");
    throw std::runtime_error(
        "Rollback failed; the installed version was restored");
  }

  std::error_code error;
  std::filesystem::remove(StatePath(targets.termify), error);
  if (error)
    throw std::runtime_error(
        "Termify was rolled back, but its rollback state could not be cleared");
  RemoveBackups(*state);
  std::filesystem::remove(currentTermify, error);
  if (!currentDownloader.empty()) {
    error.clear();
    std::filesystem::remove(currentDownloader, error);
  }
}

} // namespace termify::core
