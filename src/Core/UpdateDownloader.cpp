#include "UpdateDownloader.hpp"

#include "Sha256.hpp"
#include <algorithm>
#include <chrono>
#include <cctype>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <system_error>
#include <utility>

namespace termify::core {
namespace {

constexpr std::string_view DOWNLOAD_PREFIX =
    "https://github.com/segfaultcafe/Termify/releases/download/v";
constexpr std::string_view REPOSITORY = "segfaultcafe/Termify";
constexpr std::string_view RELEASE_WORKFLOW =
    "segfaultcafe/Termify/.github/workflows/release.yml";
constexpr std::uint64_t MAXIMUM_MANIFEST_BYTES = 1024 * 1024;
constexpr std::uint64_t MAXIMUM_ARTIFACT_BYTES = 512ULL * 1024 * 1024;

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
      throw std::runtime_error("Could not retain the verified update");
    path.clear();
  }

  std::filesystem::path path;

private:
  std::shared_ptr<platform::FileLease> _lease;
};

std::string readFile(const std::filesystem::path &path,
                     std::uint64_t maximumBytes) {
  std::error_code error;
  const auto size = std::filesystem::file_size(path, error);
  if (error || size > maximumBytes)
    throw std::runtime_error("The release checksum manifest is invalid");
  std::ifstream input(path, std::ios::binary);
  if (!input)
    throw std::runtime_error("Could not read the release checksum manifest");
  return {std::istreambuf_iterator<char>(input),
          std::istreambuf_iterator<char>()};
}

bool isHexDigest(std::string_view value) {
  return value.size() == 64 &&
         std::all_of(value.begin(), value.end(), [](unsigned char character) {
           return std::isxdigit(character) != 0;
         });
}

std::string lowercase(std::string value) {
  std::transform(value.begin(), value.end(), value.begin(),
                 [](unsigned char character) {
                   return static_cast<char>(std::tolower(character));
                 });
  return value;
}

std::string platformName(platform::ReleasePlatform value) {
  switch (value) {
  case platform::ReleasePlatform::Linux:
    return "linux";
  case platform::ReleasePlatform::MacOS:
    return "macos";
  }
  throw std::runtime_error("This operating system has no Termify update");
}

std::string architectureName(platform::ReleaseArchitecture value) {
  switch (value) {
  case platform::ReleaseArchitecture::X86_64:
    return "x86_64";
  case platform::ReleaseArchitecture::Arm64:
    return "arm64";
  case platform::ReleaseArchitecture::Unsupported:
    break;
  }
  throw std::runtime_error("This CPU architecture has no Termify update");
}

} // namespace

UpdateDownloader::UpdateDownloader(platform::ProcessRunner &processes,
                                   platform::FileSystem &files)
    : _processes(processes), _files(files) {}

PreparedUpdate
UpdateDownloader::Prepare(const UpdateCheckResult &release,
                          const platform::UpdateEnvironment &environment) const {
  if (release.state != UpdateState::Available)
    throw std::runtime_error("There is no newer stable update to download");
  const auto artifactName = ArtifactName(release.latest, environment);
  const auto version = release.latest.String();
  const auto tag = "v" + version;
  const auto baseUrl = std::string(DOWNLOAD_PREFIX) + version + "/";

  const auto directory =
      _files.UserCacheDirectory("termify") / "updates" / tag;
  std::error_code error;
  if (std::filesystem::is_symlink(directory, error))
    throw std::runtime_error("The update staging directory is not safe");
  error.clear();
  std::filesystem::create_directories(directory, error);
  if (error)
    throw std::runtime_error("Could not create the update staging directory");
  std::filesystem::permissions(directory, std::filesystem::perms::owner_all,
                               std::filesystem::perm_options::replace, error);
  if (error)
    throw std::runtime_error("Could not secure the update staging directory");

  TemporaryFile manifest(
      _files.CreateLockedFile(directory, "SHA256SUMS.part"));
  Download(baseUrl + "SHA256SUMS", manifest.path, MAXIMUM_MANIFEST_BYTES);
  VerifyAttestation(manifest.path, release.latest, directory);
  const auto expected =
      ExpectedChecksum(readFile(manifest.path, MAXIMUM_MANIFEST_BYTES),
                       artifactName);

  TemporaryFile artifact(
      _files.CreateLockedFile(directory, artifactName + ".part"));
  Download(baseUrl + artifactName, artifact.path, MAXIMUM_ARTIFACT_BYTES);
  VerifyAttestation(artifact.path, release.latest, directory);
  if (Sha256File(artifact.path) != expected)
    throw std::runtime_error("The downloaded update checksum does not match");

  const auto destination = directory / artifactName;
  const auto manifestDestination = directory / "SHA256SUMS";
  manifest.Commit(manifestDestination);
  artifact.Commit(destination);
  std::filesystem::permissions(
      destination,
      std::filesystem::perms::owner_read |
          std::filesystem::perms::owner_write,
      std::filesystem::perm_options::replace, error);
  if (error) {
    std::filesystem::remove(destination, error);
    throw std::runtime_error("Could not secure the verified update");
  }
  std::filesystem::permissions(
      manifestDestination,
      std::filesystem::perms::owner_read |
          std::filesystem::perms::owner_write,
      std::filesystem::perm_options::replace, error);
  if (error) {
    std::filesystem::remove(destination, error);
    std::filesystem::remove(manifestDestination, error);
    throw std::runtime_error("Could not secure the verified update");
  }
  return {release.latest, artifactName, destination, manifestDestination,
          environment.installation};
}

std::string
UpdateDownloader::ArtifactName(
    const Version &version, const platform::UpdateEnvironment &environment) {
  if (environment.installation == platform::InstallationKind::PackageManaged)
    throw std::runtime_error(
        "This Termify installation is managed by a package manager; use that "
        "package manager to update");
  const auto stem = "termify-v" + version.String() + "-" +
                    platformName(environment.platform) + "-" +
                    architectureName(environment.architecture);
  if (environment.installation == platform::InstallationKind::AppImage) {
    if (environment.platform != platform::ReleasePlatform::Linux)
      throw std::runtime_error("AppImage updates are only available on Linux");
    return stem + ".AppImage";
  }
  return stem + ".tar.gz";
}

std::string UpdateDownloader::ExpectedChecksum(std::string_view manifest,
                                               std::string_view artifactName) {
  std::istringstream lines{std::string(manifest)};
  std::string line;
  std::string result;
  while (std::getline(lines, line)) {
    if (!line.empty() && line.back() == '\r')
      line.pop_back();
    if (line.size() < 67 || line[64] != ' ' ||
        (line[65] != ' ' && line[65] != '*'))
      throw std::runtime_error("The release checksum manifest is invalid");
    const auto digest = std::string_view(line).substr(0, 64);
    const auto filename = std::string_view(line).substr(66);
    if (!isHexDigest(digest) || filename.empty() ||
        filename.find_first_of("/\\") != std::string_view::npos)
      throw std::runtime_error("The release checksum manifest is invalid");
    if (filename == artifactName) {
      if (!result.empty())
        throw std::runtime_error(
            "The release checksum manifest contains a duplicate artifact");
      result = lowercase(std::string(digest));
    }
  }
  if (result.empty())
    throw std::runtime_error(
        "The release checksum manifest does not contain this update");
  return result;
}

void UpdateDownloader::Download(std::string_view url,
                                const std::filesystem::path &output,
                                std::uint64_t maximumBytes) const {
  const auto curl = _files.FindExecutable("curl", {});
  if (!curl)
    throw std::runtime_error("curl is required to download Termify updates");
  const auto process = _processes.Run(
      {.executable = *curl,
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
    throw std::runtime_error("The Termify update download timed out");
  if (!process.Succeeded())
    throw std::runtime_error("Could not download the Termify update");
  std::error_code error;
  const auto size = std::filesystem::file_size(output, error);
  if (error || size == 0 || size > maximumBytes)
    throw std::runtime_error("The downloaded Termify update has an invalid size");
}

void UpdateDownloader::VerifyAttestation(
    const std::filesystem::path &subject, const Version &version,
    const std::filesystem::path &temporaryDirectory) const {
  const auto github = _files.FindExecutable("gh", {});
  if (!github)
    throw std::runtime_error(
        "GitHub CLI (gh) is required to verify release provenance");
  TemporaryFile output(
      _files.CreateLockedFile(temporaryDirectory, "attestation-output"));
  const auto process = _processes.Run(
      {.executable = *github,
       .arguments = {"attestation",
                     "verify",
                     subject.string(),
                     "--repo",
                     std::string(REPOSITORY),
                     "--signer-workflow",
                     std::string(RELEASE_WORKFLOW),
                     "--source-ref",
                     "refs/tags/v" + version.String(),
                     "--deny-self-hosted-runners"},
       .standardOutput = output.path,
       .standardError = std::nullopt,
       .discardStandardError = true},
      nullptr, std::chrono::seconds(120));
  if (process.completion == platform::ProcessCompletion::TimedOut)
    throw std::runtime_error("Release provenance verification timed out");
  if (!process.Succeeded())
    throw std::runtime_error(
        "The downloaded update has no valid Termify release provenance");
}

} // namespace termify::core
