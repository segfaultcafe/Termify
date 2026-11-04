#include "Core/Sha256.hpp"
#include "Core/YtDlpLocator.hpp"
#include "Core/YtDlpUpdater.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

using termify::core::FindYtDlpExecutable;
using termify::core::ManagedYtDlpPath;
using termify::core::Sha256;
using termify::core::YtDlpUpdater;
using termify::core::YtDlpUpdateState;
using termify::core::YtDlpVersion;
using termify::platform::InstallationKind;
using termify::platform::ReleaseArchitecture;
using termify::platform::ReleasePlatform;
using termify::platform::UpdateEnvironment;

#define CHECK(condition)                                                       \
  do {                                                                         \
    if (!(condition)) {                                                        \
      std::cerr << "Check failed at line " << __LINE__ << ": " #condition      \
                << '\n';                                                       \
      return 1;                                                                \
    }                                                                          \
  } while (false)

template <typename Callback> bool throws(Callback callback) {
  try {
    callback();
    return false;
  } catch (const std::runtime_error &) {
    return true;
  }
}

namespace {

void writeFile(const std::filesystem::path &path, std::string_view value) {
  std::filesystem::create_directories(path.parent_path());
  std::ofstream(path, std::ios::binary) << value;
  std::filesystem::permissions(path, std::filesystem::perms::owner_all,
                               std::filesystem::perm_options::replace);
}

std::string readFile(const std::filesystem::path &path) {
  std::ifstream input(path, std::ios::binary);
  return {std::istreambuf_iterator<char>(input),
          std::istreambuf_iterator<char>()};
}

class TestLease final : public termify::platform::FileLease {};

class TestFileSystem final : public termify::platform::FileSystem {
public:
  explicit TestFileSystem(std::filesystem::path value)
      : root(std::move(value)) {}

  std::filesystem::path ExecutableDirectory() const override {
    return root / "bin";
  }
  std::filesystem::path TemporaryDirectory() const override {
    const auto result = root / "temporary";
    std::filesystem::create_directories(result);
    return result;
  }
  std::optional<std::filesystem::path> FindExecutable(
      const std::string &name,
      const std::vector<std::filesystem::path> &preferred) const override {
    if (name == "curl" && provideCurl)
      return "/fake/curl";
    if (name == "gh" && provideGithub)
      return "/fake/gh";
    for (const auto &path : preferred) {
      if (std::filesystem::is_regular_file(path))
        return path;
    }
    return std::nullopt;
  }
  std::filesystem::path TemporaryStem(const std::string &prefix) override {
    return root / "temporary" / prefix;
  }
  std::filesystem::path UserDataDirectory(const std::string &) const override {
    return root / "data";
  }
  std::filesystem::path UserCacheDirectory(const std::string &) const override {
    return root / "cache";
  }
  std::shared_ptr<termify::platform::FileLease>
  TryLock(const std::filesystem::path &,
          termify::platform::FileLockMode) const override {
    return std::make_shared<TestLease>();
  }
  termify::platform::LockedFile
  CreateLockedFile(const std::filesystem::path &directory,
                   const std::string &prefix) const override {
    std::filesystem::create_directories(directory);
    const auto path =
        directory / (prefix + "-test-" + std::to_string(sequence++));
    std::ofstream(path).close();
    return {path, std::make_shared<TestLease>()};
  }
  std::filesystem::path
  CreatePrivateDirectory(const std::filesystem::path &,
                         const std::string &) const override {
    throw std::runtime_error("unused");
  }
  void ReplaceFileAtomically(const std::filesystem::path &source,
                             const std::filesystem::path &destination,
                             bool executable) const override {
    if (failDestination && destination == *failDestination) {
      failDestination.reset();
      throw std::runtime_error("injected replacement failure");
    }
    std::filesystem::create_directories(destination.parent_path());
    const auto temporary =
        destination.string() + ".replacement-" + std::to_string(sequence++);
    std::filesystem::copy_file(
        source, temporary, std::filesystem::copy_options::overwrite_existing);
    std::filesystem::permissions(temporary,
                                 executable
                                     ? std::filesystem::perms::owner_all
                                     : std::filesystem::perms::owner_read |
                                           std::filesystem::perms::owner_write,
                                 std::filesystem::perm_options::replace);
    std::error_code error;
    std::filesystem::remove(destination, error);
    std::filesystem::rename(temporary, destination);
  }

  std::filesystem::path root;
  bool provideCurl{true};
  bool provideGithub{true};
  mutable std::optional<std::filesystem::path> failDestination;

private:
  mutable unsigned sequence{0};
};

std::optional<std::string>
argumentAfter(const std::vector<std::string> &arguments,
              const std::string &option) {
  const auto item = std::find(arguments.begin(), arguments.end(), option);
  if (item == arguments.end() || std::next(item) == arguments.end())
    return std::nullopt;
  return *std::next(item);
}

class TestProcessRunner final : public termify::platform::ProcessRunner {
public:
  termify::platform::ProcessResult
  Run(const termify::platform::ProcessRequest &request,
      const std::atomic_bool *,
      std::optional<std::chrono::milliseconds>) override {
    requests.push_back(request);
    if (request.executable == "/fake/gh") {
      ++attestations;
      return {termify::platform::ProcessCompletion::Exited,
              failAttestation ? 1 : 0};
    }
    if (request.executable == "/fake/curl") {
      const auto output = argumentAfter(request.arguments, "--output");
      if (!output)
        return {termify::platform::ProcessCompletion::Exited, 2};
      const auto &url = request.arguments.back();
      if (url.find("matching-refs") != std::string::npos)
        std::ofstream(*output) << refs;
      else if (url.ends_with("/SHA256SUMS"))
        std::ofstream(*output) << manifest;
      else
        std::ofstream(*output) << artifactVersion;
      return {termify::platform::ProcessCompletion::Exited, 0};
    }
    if (request.arguments == std::vector<std::string>{"--version"} &&
        request.standardOutput) {
      std::ofstream(*request.standardOutput)
          << readFile(request.executable) << '\n';
      return {termify::platform::ProcessCompletion::Exited, 0};
    }
    return {termify::platform::ProcessCompletion::Exited, 2};
  }

  void SelectRelease(std::string version, std::string artifactName) {
    refs = "[{\"ref\":\"refs/tags/yt-dlp-v" + version + "\"}]";
    artifactVersion = std::move(version);
    manifest = Sha256(artifactVersion) + "  " + artifactName + "\n";
  }

  std::string refs;
  std::string artifactVersion;
  std::string manifest;
  bool failAttestation{false};
  unsigned attestations{0};
  std::vector<termify::platform::ProcessRequest> requests;
};

UpdateEnvironment
environment(InstallationKind installation = InstallationKind::UserLocal) {
  return {ReleasePlatform::Linux, ReleaseArchitecture::X86_64, installation,
          "/fixture/bin/termify"};
}

} // namespace

int main() {
  CHECK(YtDlpVersion::Parse("2026.10.26").String() == "2026.10.26");
  CHECK(YtDlpVersion::Parse("2026.10.26.2").String() == "2026.10.26.2");
  CHECK(YtDlpVersion::Parse("2026.10.27") >
        YtDlpVersion::Parse("2026.10.26.9"));
  CHECK(throws([] { YtDlpVersion::Parse("2026.1.01"); }));
  CHECK(throws([] { YtDlpVersion::Parse("2026.13.01"); }));
  CHECK(throws([] { YtDlpVersion::Parse("2026.10.26."); }));
  CHECK(throws([] { YtDlpVersion::Parse("2026.10.26.1.2"); }));
  CHECK(YtDlpUpdater::LatestFromRefs(
            "[{\"ref\":\"refs/tags/yt-dlp-v2026.10.26\"},"
            "{\"ref\":\"refs/tags/yt-dlp-v2026.11.01.1\"}]") ==
        YtDlpVersion::Parse("2026.11.01.1"));
  CHECK(throws([] { YtDlpUpdater::LatestFromRefs("[]"); }));

  const auto linux = environment();
  CHECK(YtDlpUpdater::ArtifactName(YtDlpVersion::Parse("2026.11.01"), linux) ==
        "termify-yt-dlp-2026.11.01-linux-x86_64");
  CHECK(YtDlpUpdater::ArtifactName(YtDlpVersion::Parse("2026.11.01"),
                                   {ReleasePlatform::Linux,
                                    ReleaseArchitecture::Arm64,
                                    InstallationKind::AppImage,
                                    {}}) ==
        "termify-yt-dlp-2026.11.01-linux-arm64");
  CHECK(YtDlpUpdater::ArtifactName(YtDlpVersion::Parse("2026.11.01"),
                                   {ReleasePlatform::MacOS,
                                    ReleaseArchitecture::Arm64,
                                    InstallationKind::UserLocal,
                                    {}}) ==
        "termify-yt-dlp-2026.11.01-macos-universal");

  const auto root =
      std::filesystem::temp_directory_path() / "termify-ytdlp-updater-tests";
  std::filesystem::remove_all(root);
  writeFile(root / "bin/yt-dlp", "2026.10.26");
  TestFileSystem files(root);
  TestProcessRunner processes;
  YtDlpUpdater updater(processes, files);
  const auto artifact = "termify-yt-dlp-2026.11.01-linux-x86_64";
  processes.SelectRelease("2026.11.01", artifact);

  const auto check = updater.Check(linux);
  CHECK(check.state == YtDlpUpdateState::Available);
  CHECK(check.current == YtDlpVersion::Parse("2026.10.26"));
  CHECK(check.latest == YtDlpVersion::Parse("2026.11.01"));
  const auto prepared = updater.Prepare(check, linux);
  CHECK(prepared.artifactName == artifact);
  CHECK(processes.attestations == 2);
  CHECK(std::filesystem::exists(prepared.path));
  CHECK(std::any_of(processes.requests.begin(), processes.requests.end(),
                    [](const auto &request) {
                      return std::find(request.arguments.begin(),
                                       request.arguments.end(),
                                       "refs/heads/master") !=
                             request.arguments.end();
                    }));

  updater.Install(prepared, linux);
  CHECK(readFile(ManagedYtDlpPath(files)) == "2026.11.01");
  CHECK(FindYtDlpExecutable(files, linux) == ManagedYtDlpPath(files));
  CHECK(updater.Check(linux).state == YtDlpUpdateState::UpToDate);
  const auto statePath = root / "data/yt-dlp/rollback-state";
  auto pendingState = readFile(statePath);
  const auto ready = pendingState.find("\nready\n");
  CHECK(ready != std::string::npos);
  pendingState.replace(ready, std::string("\nready\n").size(), "\npending\n");
  writeFile(statePath, pendingState);
  CHECK(throws([&] { updater.Install(prepared, linux); }));
  updater.Rollback(linux);
  CHECK(!std::filesystem::exists(ManagedYtDlpPath(files)));
  CHECK(FindYtDlpExecutable(files, linux) == root / "bin/yt-dlp");
  CHECK(throws([&] { updater.Rollback(linux); }));

  writeFile(ManagedYtDlpPath(files), "2026.11.01");
  processes.SelectRelease("2026.12.01",
                          "termify-yt-dlp-2026.12.01-linux-x86_64");
  const auto second = updater.Check(linux);
  const auto secondPrepared = updater.Prepare(second, linux);
  updater.Install(secondPrepared, linux);
  CHECK(readFile(ManagedYtDlpPath(files)) == "2026.12.01");
  updater.Rollback(linux);
  CHECK(readFile(ManagedYtDlpPath(files)) == "2026.11.01");

  processes.SelectRelease("2027.01.01",
                          "termify-yt-dlp-2027.01.01-linux-x86_64");
  const auto failedCheck = updater.Check(linux);
  const auto failedPrepared = updater.Prepare(failedCheck, linux);
  files.failDestination = ManagedYtDlpPath(files);
  CHECK(throws([&] { updater.Install(failedPrepared, linux); }));
  CHECK(readFile(ManagedYtDlpPath(files)) == "2026.11.01");

  const auto managed = environment(InstallationKind::PackageManaged);
  CHECK(throws([&] { updater.Check(managed); }));
  CHECK(throws([&] {
    YtDlpUpdater::ArtifactName(YtDlpVersion::Parse("2027.01.01"), managed);
  }));
  CHECK(FindYtDlpExecutable(files, managed) == root / "bin/yt-dlp");

  std::filesystem::remove_all(root);
  std::cout << "yt-dlp updater tests passed.\n";
  return 0;
}
