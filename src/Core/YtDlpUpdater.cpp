#include "YtDlpUpdater.hpp"

#include "ChecksumManifest.hpp"
#include "Sha256.hpp"
#include "YtDlpLocator.hpp"

#include <algorithm>
#include <charconv>
#include <chrono>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <system_error>
#include <utility>

namespace termify::core {
namespace {

constexpr std::string_view REFS_URL =
    "https://api.github.com/repos/segfaultcafe/Termify/git/"
    "matching-refs/tags/yt-dlp-v";
constexpr std::string_view DOWNLOAD_PREFIX =
    "https://github.com/segfaultcafe/Termify/releases/download/yt-dlp-v";
constexpr std::string_view REPOSITORY = "segfaultcafe/Termify";
constexpr std::string_view RELEASE_WORKFLOW =
    "segfaultcafe/Termify/.github/workflows/yt-dlp-release.yml";
constexpr std::string_view STATE_HEADER = "TERMIFY_YT_DLP_ROLLBACK_V1";
constexpr std::uint64_t MAXIMUM_REFS_BYTES = 1024 * 1024;
constexpr std::uint64_t MAXIMUM_MANIFEST_BYTES = 64 * 1024;
constexpr std::uint64_t MAXIMUM_ARTIFACT_BYTES = 128ULL * 1024 * 1024;

class TemporaryFile {
public:
  explicit TemporaryFile(platform::LockedFile file)
      : path(std::move(file.path)), _lease(std::move(file.lease)) {}
  ~TemporaryFile() {
    if (path.empty())
      return;
    std::error_code error;
    std::filesystem::remove(path, error);
  }
  TemporaryFile(const TemporaryFile &) = delete;
  TemporaryFile &operator=(const TemporaryFile &) = delete;

  void Commit(const std::filesystem::path &destination) {
    std::error_code error;
    std::filesystem::remove(destination, error);
    error.clear();
    std::filesystem::rename(path, destination, error);
    if (error)
      throw std::runtime_error("Could not retain the verified yt-dlp update");
    path.clear();
  }

  std::filesystem::path path;

private:
  std::shared_ptr<platform::FileLease> _lease;
};

bool isRegularFile(const std::filesystem::path &path) {
  std::error_code error;
  const auto status = std::filesystem::symlink_status(path, error);
  return !error && std::filesystem::is_regular_file(status) &&
         std::filesystem::file_size(path, error) > 0 && !error;
}

std::string readFile(const std::filesystem::path &path,
                     std::uint64_t maximumBytes, std::string_view description) {
  std::error_code error;
  const auto size = std::filesystem::file_size(path, error);
  if (error || size > maximumBytes)
    throw std::runtime_error(std::string(description) + " is invalid");
  std::ifstream input(path, std::ios::binary);
  if (!input)
    throw std::runtime_error("Could not read " + std::string(description));
  return {std::istreambuf_iterator<char>(input),
          std::istreambuf_iterator<char>()};
}

std::string trim(std::string value) {
  while (!value.empty() && (value.back() == '\n' || value.back() == '\r' ||
                            value.back() == ' ' || value.back() == '\t'))
    value.pop_back();
  const auto first = value.find_first_not_of(" \t\r\n");
  if (first == std::string::npos)
    return {};
  value.erase(0, first);
  return value;
}

unsigned parsePart(std::string_view value) {
  if (value.empty())
    throw std::runtime_error("Invalid yt-dlp version");
  unsigned result = 0;
  const auto parsed =
      std::from_chars(value.data(), value.data() + value.size(), result);
  if (parsed.ec != std::errc{} || parsed.ptr != value.data() + value.size())
    throw std::runtime_error("Invalid yt-dlp version");
  return result;
}

std::string platformName(platform::ReleasePlatform value) {
  switch (value) {
  case platform::ReleasePlatform::Linux:
    return "linux";
  case platform::ReleasePlatform::MacOS:
    return "macos";
  }
  throw std::runtime_error("This operating system has no yt-dlp update");
}

std::string architectureName(platform::ReleasePlatform platform,
                             platform::ReleaseArchitecture architecture) {
  if (platform == platform::ReleasePlatform::MacOS &&
      architecture != platform::ReleaseArchitecture::Unsupported)
    return "universal";
  switch (architecture) {
  case platform::ReleaseArchitecture::X86_64:
    return "x86_64";
  case platform::ReleaseArchitecture::Arm64:
    return "arm64";
  case platform::ReleaseArchitecture::Unsupported:
    break;
  }
  throw std::runtime_error("This CPU architecture has no yt-dlp update");
}

} // namespace

YtDlpVersion YtDlpVersion::Parse(std::string_view value) {
  if (value.starts_with('v'))
    value.remove_prefix(1);
  std::string_view parts[4];
  std::size_t count = 0;
  std::size_t start = 0;
  while (count < 4) {
    const auto separator = value.find('.', start);
    parts[count++] = value.substr(start, separator - start);
    if (separator == std::string_view::npos) {
      start = value.size();
      break;
    }
    start = separator + 1;
  }
  if (start != value.size() || count < 3 || parts[0].size() != 4 ||
      parts[1].size() != 2 || parts[2].size() != 2)
    throw std::runtime_error("Invalid yt-dlp version");
  YtDlpVersion result{parsePart(parts[0]), parsePart(parts[1]),
                      parsePart(parts[2]),
                      count == 4 ? parsePart(parts[3]) : 0};
  if (result.year < 2000 || result.month < 1 || result.month > 12 ||
      result.day < 1 || result.day > 31 || (count == 4 && result.revision == 0))
    throw std::runtime_error("Invalid yt-dlp version");
  return result;
}

std::string YtDlpVersion::String() const {
  std::ostringstream output;
  output << std::setfill('0') << std::setw(4) << year << '.' << std::setw(2)
         << month << '.' << std::setw(2) << day;
  if (revision != 0)
    output << '.' << revision;
  return output.str();
}

YtDlpUpdater::YtDlpUpdater(platform::ProcessRunner &processes,
                           platform::FileSystem &files)
    : _processes(processes), _files(files) {}

void YtDlpUpdater::RequireUnmanaged(
    const platform::UpdateEnvironment &environment) {
  if (environment.installation == platform::InstallationKind::PackageManaged)
    throw std::runtime_error(
        "This Termify installation is managed by a package manager; use that "
        "package manager to update yt-dlp");
}

YtDlpUpdateCheckResult
YtDlpUpdater::Check(const platform::UpdateEnvironment &environment) const {
  RequireUnmanaged(environment);
  const auto installed = FindYtDlpExecutable(_files, environment);
  if (installed.empty())
    throw std::runtime_error(
        "The installed yt-dlp executable could not be located");
  const auto current =
      ExecutableVersion(installed, _files.TemporaryDirectory());
  const auto latest = LatestFromRefs(FetchReleaseRefs());
  YtDlpUpdateCheckResult result{
      YtDlpUpdateState::UpToDate, current, latest,
      std::string("https://github.com/segfaultcafe/Termify/releases/tag/") +
          "yt-dlp-v" + latest.String()};
  if (latest > current)
    result.state = YtDlpUpdateState::Available;
  else if (latest < current)
    result.state = YtDlpUpdateState::DevelopmentBuild;
  return result;
}

YtDlpVersion YtDlpUpdater::LatestFromRefs(std::string_view response) {
  constexpr std::string_view prefix = "refs/tags/yt-dlp-v";
  std::optional<YtDlpVersion> latest;
  std::size_t position = 0;
  while ((position = response.find(prefix, position)) !=
         std::string_view::npos) {
    const auto start = position + prefix.size();
    const auto end = response.find('"', start);
    if (end == std::string_view::npos)
      throw std::runtime_error(
          "GitHub returned invalid yt-dlp release metadata");
    const auto candidate =
        YtDlpVersion::Parse(response.substr(start, end - start));
    if (!latest || candidate > *latest)
      latest = candidate;
    position = end + 1;
  }
  if (!latest)
    throw std::runtime_error("No independent yt-dlp release is available");
  return *latest;
}

std::string
YtDlpUpdater::ArtifactName(const YtDlpVersion &version,
                           const platform::UpdateEnvironment &environment) {
  RequireUnmanaged(environment);
  return "termify-yt-dlp-" + version.String() + "-" +
         platformName(environment.platform) + "-" +
         architectureName(environment.platform, environment.architecture);
}

std::string YtDlpUpdater::Format(const YtDlpUpdateCheckResult &result) {
  std::ostringstream output;
  switch (result.state) {
  case YtDlpUpdateState::Available:
    output << "yt-dlp " << result.latest.String()
           << " is available (installed: " << result.current.String()
           << ").\nRelease: " << result.releaseUrl << '\n';
    break;
  case YtDlpUpdateState::DevelopmentBuild:
    output << "Installed yt-dlp " << result.current.String()
           << " is newer than Termify's update channel ("
           << result.latest.String() << ").\n";
    break;
  case YtDlpUpdateState::UpToDate:
    output << "yt-dlp " << result.current.String() << " is up to date.\n";
    break;
  }
  return output.str();
}

std::string YtDlpUpdater::FetchReleaseRefs() const {
  const auto curl = _files.FindExecutable("curl", {});
  if (!curl)
    throw std::runtime_error("curl is required to check for yt-dlp updates");
  TemporaryFile response(
      _files.CreateLockedFile(_files.TemporaryDirectory(), "yt-dlp-refs"));
  const auto process =
      _processes.Run({.executable = *curl,
                      .arguments = {"--fail",
                                    "--silent",
                                    "--show-error",
                                    "--location",
                                    "--max-redirs",
                                    "5",
                                    "--connect-timeout",
                                    "5",
                                    "--max-time",
                                    "20",
                                    "--max-filesize",
                                    std::to_string(MAXIMUM_REFS_BYTES),
                                    "--proto",
                                    "=https",
                                    "--proto-redir",
                                    "=https",
                                    "--tlsv1.2",
                                    "--header",
                                    "Accept: application/vnd.github+json",
                                    "--header",
                                    "X-GitHub-Api-Version: 2022-11-28",
                                    "--output",
                                    response.path.string(),
                                    std::string(REFS_URL)},
                      .standardOutput = std::nullopt,
                      .standardError = std::nullopt,
                      .discardStandardError = true},
                     nullptr, std::chrono::seconds(25));
  if (process.completion == platform::ProcessCompletion::TimedOut)
    throw std::runtime_error("The yt-dlp update check timed out");
  if (!process.Succeeded())
    throw std::runtime_error("Could not contact the yt-dlp update service");
  return readFile(response.path, MAXIMUM_REFS_BYTES,
                  "the yt-dlp update response");
}

YtDlpVersion YtDlpUpdater::ExecutableVersion(
    const std::filesystem::path &executable,
    const std::filesystem::path &temporaryDirectory) const {
  TemporaryFile output(
      _files.CreateLockedFile(temporaryDirectory, "yt-dlp-version"));
  const auto process = _processes.Run({.executable = executable,
                                       .arguments = {"--version"},
                                       .standardOutput = output.path,
                                       .standardError = std::nullopt,
                                       .discardStandardError = true},
                                      nullptr, std::chrono::seconds(20));
  if (!process.Succeeded())
    throw std::runtime_error("Could not read the installed yt-dlp version");
  return YtDlpVersion::Parse(
      trim(readFile(output.path, 128, "the yt-dlp version")));
}

PreparedYtDlpUpdate
YtDlpUpdater::Prepare(const YtDlpUpdateCheckResult &release,
                      const platform::UpdateEnvironment &environment) const {
  RequireUnmanaged(environment);
  if (release.state != YtDlpUpdateState::Available)
    throw std::runtime_error("There is no newer yt-dlp update to install");
  const auto artifactName = ArtifactName(release.latest, environment);
  const auto tag = "yt-dlp-v" + release.latest.String();
  const auto baseUrl =
      std::string(DOWNLOAD_PREFIX) + release.latest.String() + "/";
  const auto directory = _files.UserCacheDirectory("termify") / "updates" / tag;
  std::error_code error;
  if (std::filesystem::is_symlink(directory, error))
    throw std::runtime_error("The yt-dlp staging directory is not safe");
  error.clear();
  std::filesystem::create_directories(directory, error);
  if (error)
    throw std::runtime_error("Could not create the yt-dlp staging directory");
  std::filesystem::permissions(directory, std::filesystem::perms::owner_all,
                               std::filesystem::perm_options::replace, error);
  if (error)
    throw std::runtime_error("Could not secure the yt-dlp staging directory");

  TemporaryFile manifest(_files.CreateLockedFile(directory, "SHA256SUMS.part"));
  Download(baseUrl + "SHA256SUMS", manifest.path, MAXIMUM_MANIFEST_BYTES);
  VerifyAttestation(manifest.path, directory);
  const auto expected =
      ChecksumForArtifact(readFile(manifest.path, MAXIMUM_MANIFEST_BYTES,
                                   "the yt-dlp checksum manifest"),
                          artifactName);

  TemporaryFile artifact(
      _files.CreateLockedFile(directory, artifactName + ".part"));
  Download(baseUrl + artifactName, artifact.path, MAXIMUM_ARTIFACT_BYTES);
  VerifyAttestation(artifact.path, directory);
  if (Sha256File(artifact.path) != expected)
    throw std::runtime_error("The downloaded yt-dlp checksum does not match");
  std::filesystem::permissions(artifact.path, std::filesystem::perms::owner_all,
                               std::filesystem::perm_options::replace, error);
  if (error)
    throw std::runtime_error("Could not secure the verified yt-dlp update");
  if (ExecutableVersion(artifact.path, directory) != release.latest)
    throw std::runtime_error("The downloaded yt-dlp reports the wrong version");

  const auto destination = directory / artifactName;
  const auto manifestDestination = directory / "SHA256SUMS";
  manifest.Commit(manifestDestination);
  artifact.Commit(destination);
  return {release.latest,      artifactName,         destination,
          manifestDestination, environment.platform, environment.architecture};
}

void YtDlpUpdater::Download(std::string_view url,
                            const std::filesystem::path &output,
                            std::uint64_t maximumBytes) const {
  const auto curl = _files.FindExecutable("curl", {});
  if (!curl)
    throw std::runtime_error("curl is required to download yt-dlp updates");
  const auto process =
      _processes.Run({.executable = *curl,
                      .arguments = {"--fail",
                                    "--silent",
                                    "--show-error",
                                    "--location",
                                    "--retry",
                                    "2",
                                    "--max-redirs",
                                    "5",
                                    "--connect-timeout",
                                    "10",
                                    "--max-time",
                                    "600",
                                    "--max-filesize",
                                    std::to_string(maximumBytes),
                                    "--proto",
                                    "=https",
                                    "--proto-redir",
                                    "=https",
                                    "--tlsv1.2",
                                    "--output",
                                    output.string(),
                                    std::string(url)},
                      .standardOutput = std::nullopt,
                      .standardError = std::nullopt,
                      .discardStandardError = true},
                     nullptr, std::chrono::seconds(610));
  if (process.completion == platform::ProcessCompletion::TimedOut)
    throw std::runtime_error("The yt-dlp update download timed out");
  if (!process.Succeeded() || !isRegularFile(output))
    throw std::runtime_error("Could not download the yt-dlp update");
  std::error_code error;
  if (std::filesystem::file_size(output, error) > maximumBytes || error)
    throw std::runtime_error(
        "The downloaded yt-dlp update has an invalid size");
}

void YtDlpUpdater::VerifyAttestation(
    const std::filesystem::path &subject,
    const std::filesystem::path &temporaryDirectory) const {
  const auto github = _files.FindExecutable("gh", {});
  if (!github)
    throw std::runtime_error(
        "GitHub CLI (gh) is required to verify yt-dlp provenance");
  TemporaryFile output(
      _files.CreateLockedFile(temporaryDirectory, "attestation-output"));
  const auto process = _processes.Run(
      {.executable = *github,
       .arguments = {"attestation", "verify", subject.string(), "--repo",
                     std::string(REPOSITORY), "--signer-workflow",
                     std::string(RELEASE_WORKFLOW), "--source-ref",
                     "refs/heads/master", "--deny-self-hosted-runners"},
       .standardOutput = output.path,
       .standardError = std::nullopt,
       .discardStandardError = true},
      nullptr, std::chrono::seconds(120));
  if (process.completion == platform::ProcessCompletion::TimedOut)
    throw std::runtime_error("yt-dlp provenance verification timed out");
  if (!process.Succeeded())
    throw std::runtime_error(
        "The downloaded yt-dlp has no valid Termify provenance");
}

std::filesystem::path YtDlpUpdater::DataDirectory() const {
  const auto directory = ManagedYtDlpPath(_files).parent_path();
  std::error_code error;
  if (std::filesystem::is_symlink(directory, error))
    throw std::runtime_error("The yt-dlp data directory is not safe");
  error.clear();
  std::filesystem::create_directories(directory, error);
  if (error)
    throw std::runtime_error("Could not create the yt-dlp data directory");
  std::filesystem::permissions(directory, std::filesystem::perms::owner_all,
                               std::filesystem::perm_options::replace, error);
  if (error)
    throw std::runtime_error("Could not secure the yt-dlp data directory");
  return directory;
}

std::filesystem::path YtDlpUpdater::RollbackDirectory() const {
  const auto directory =
      _files.UserCacheDirectory("termify") / "updates" / "yt-dlp-rollback";
  std::error_code error;
  if (std::filesystem::is_symlink(directory, error))
    throw std::runtime_error("The yt-dlp rollback directory is not safe");
  error.clear();
  std::filesystem::create_directories(directory, error);
  if (error)
    throw std::runtime_error("Could not create the yt-dlp rollback directory");
  std::filesystem::permissions(directory, std::filesystem::perms::owner_all,
                               std::filesystem::perm_options::replace, error);
  if (error)
    throw std::runtime_error("Could not secure the yt-dlp rollback directory");
  return directory;
}

std::shared_ptr<platform::FileLease>
YtDlpUpdater::AcquireLock(const std::filesystem::path &directory) const {
  const auto path = directory / "update.lock";
  std::error_code error;
  if (std::filesystem::is_symlink(path, error))
    throw std::runtime_error("The yt-dlp update lock is not safe");
  {
    std::ofstream output(path, std::ios::app);
    if (!output)
      throw std::runtime_error("Could not create the yt-dlp update lock");
  }
  std::filesystem::permissions(path,
                               std::filesystem::perms::owner_read |
                                   std::filesystem::perms::owner_write,
                               std::filesystem::perm_options::replace, error);
  if (error)
    throw std::runtime_error("Could not secure the yt-dlp update lock");
  auto lease = _files.TryLock(path, platform::FileLockMode::Exclusive);
  if (!lease)
    throw std::runtime_error("Another yt-dlp update is already running");
  return lease;
}

std::optional<YtDlpUpdater::RollbackState>
YtDlpUpdater::LoadState(const std::filesystem::path &directory) const {
  const auto path = directory / "rollback-state";
  std::error_code error;
  if (!std::filesystem::exists(path, error))
    return std::nullopt;
  if (!isRegularFile(path) || std::filesystem::file_size(path, error) > 4096)
    throw std::runtime_error("The yt-dlp rollback state is invalid");
  std::ifstream input(path);
  std::string header;
  std::string phase;
  std::string backup;
  std::string extra;
  if (!std::getline(input, header) || !std::getline(input, phase) ||
      !std::getline(input, backup) || std::getline(input, extra) ||
      header != STATE_HEADER || (phase != "pending" && phase != "ready") ||
      (!backup.empty() &&
       std::filesystem::path(backup).filename().string() != backup))
    throw std::runtime_error("The yt-dlp rollback state is invalid");
  return RollbackState{backup, phase == "ready"};
}

void YtDlpUpdater::SaveState(const std::filesystem::path &directory,
                             const RollbackState &state) const {
  const auto path = directory / "rollback-state";
  auto temporary = _files.CreateLockedFile(directory, "rollback-state.part");
  {
    std::ofstream output(temporary.path, std::ios::trunc);
    output << STATE_HEADER << '\n'
           << (state.committed ? "ready" : "pending") << '\n'
           << state.backup << '\n';
    if (!output)
      throw std::runtime_error("Could not write the yt-dlp rollback state");
  }
  _files.ReplaceFileAtomically(temporary.path, path, false);
  std::error_code error;
  std::filesystem::remove(temporary.path, error);
}

std::filesystem::path
YtDlpUpdater::Snapshot(const std::filesystem::path &source) const {
  auto temporary =
      _files.CreateLockedFile(RollbackDirectory(), "yt-dlp.previous");
  _files.ReplaceFileAtomically(source, temporary.path, false);
  return temporary.path;
}

void YtDlpUpdater::RemoveBackup(const RollbackState &state) const {
  if (state.backup.empty())
    return;
  std::error_code error;
  std::filesystem::remove(RollbackDirectory() / state.backup, error);
}

void YtDlpUpdater::Install(
    const PreparedYtDlpUpdate &update,
    const platform::UpdateEnvironment &environment) const {
  RequireUnmanaged(environment);
  if (update.platform != environment.platform ||
      update.architecture != environment.architecture ||
      update.artifactName != ArtifactName(update.version, environment) ||
      !isRegularFile(update.path))
    throw std::runtime_error("The verified yt-dlp update is not valid here");

  const auto directory = DataDirectory();
  const auto lock = AcquireLock(directory);
  const auto target = ManagedYtDlpPath(_files);
  const auto previousState = LoadState(directory);
  if (previousState && !previousState->committed)
    throw std::runtime_error(
        "A yt-dlp update was interrupted; run 'termify update yt-dlp "
        "--rollback' before trying again");

  std::filesystem::path backup;
  std::error_code error;
  if (std::filesystem::exists(target, error)) {
    if (!isRegularFile(target))
      throw std::runtime_error("The managed yt-dlp executable is not safe");
    backup = Snapshot(target);
  }
  RollbackState next{
      backup.empty() ? std::string() : backup.filename().string(), false};
  try {
    SaveState(directory, next);
  } catch (...) {
    if (!backup.empty())
      std::filesystem::remove(backup, error);
    throw;
  }
  try {
    _files.ReplaceFileAtomically(update.path, target, true);
    if (ExecutableVersion(target, directory) != update.version)
      throw std::runtime_error(
          "The installed yt-dlp reports the wrong version");
    next.committed = true;
    SaveState(directory, next);
  } catch (...) {
    bool restored = true;
    try {
      if (!backup.empty())
        _files.ReplaceFileAtomically(backup, target, true);
      else if (!std::filesystem::remove(target, error) && error)
        throw std::runtime_error("Could not remove the failed yt-dlp update");
    } catch (...) {
      restored = false;
    }
    try {
      if (previousState)
        SaveState(directory, *previousState);
      else
        std::filesystem::remove(directory / "rollback-state", error);
    } catch (...) {
      restored = false;
    }
    if (!backup.empty())
      std::filesystem::remove(backup, error);
    if (!restored)
      throw std::runtime_error(
          "yt-dlp installation failed and automatic restoration also failed");
    throw;
  }
  if (previousState)
    RemoveBackup(*previousState);
}

void YtDlpUpdater::Rollback(
    const platform::UpdateEnvironment &environment) const {
  RequireUnmanaged(environment);
  const auto directory = DataDirectory();
  const auto lock = AcquireLock(directory);
  const auto state = LoadState(directory);
  if (!state)
    throw std::runtime_error("There is no yt-dlp update to roll back");
  const auto target = ManagedYtDlpPath(_files);
  const auto backup = state->backup.empty()
                          ? std::filesystem::path()
                          : RollbackDirectory() / state->backup;
  if (!backup.empty() && !isRegularFile(backup))
    throw std::runtime_error("The previous yt-dlp version is unavailable");

  std::filesystem::path current;
  if (isRegularFile(target))
    current = Snapshot(target);
  std::error_code error;
  try {
    if (!backup.empty())
      _files.ReplaceFileAtomically(backup, target, true);
    else if (!std::filesystem::remove(target, error) && error)
      throw std::runtime_error("Could not remove the managed yt-dlp update");
  } catch (...) {
    if (!current.empty()) {
      try {
        _files.ReplaceFileAtomically(current, target, true);
      } catch (...) {
        throw std::runtime_error(
            "yt-dlp rollback failed and the installed version could not be "
            "restored");
      }
    }
    throw;
  }
  std::filesystem::remove(directory / "rollback-state", error);
  if (error)
    throw std::runtime_error(
        "yt-dlp was rolled back, but its rollback state could not be cleared");
  RemoveBackup(*state);
  if (!current.empty())
    std::filesystem::remove(current, error);
}

} // namespace termify::core
