#include "Core/UpdateInstaller.hpp"

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

using termify::core::PreparedUpdate;
using termify::core::UpdateInstaller;
using termify::core::Version;
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

  std::filesystem::path ExecutableDirectory() const override { return root; }
  std::filesystem::path TemporaryDirectory() const override { return root; }
  std::optional<std::filesystem::path>
  FindExecutable(const std::string &name,
                 const std::vector<std::filesystem::path> &) const override {
    if (name == "tar")
      return "/fake/tar";
    return std::nullopt;
  }
  std::filesystem::path TemporaryStem(const std::string &prefix) override {
    return root / (prefix + "-temporary");
  }
  std::filesystem::path UserDataDirectory(const std::string &) const override {
    return root;
  }
  std::filesystem::path UserCacheDirectory(const std::string &) const override {
    return root;
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
  CreatePrivateDirectory(const std::filesystem::path &directory,
                         const std::string &prefix) const override {
    const auto path =
        directory / (prefix + "-test-" + std::to_string(sequence++));
    std::filesystem::create_directories(path);
    return path;
  }
  void ReplaceFileAtomically(const std::filesystem::path &source,
                             const std::filesystem::path &destination,
                             bool executable) const override {
    if (failOnceDestination && destination == *failOnceDestination &&
        !replacementFailed) {
      replacementFailed = true;
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
  mutable std::optional<std::filesystem::path> failOnceDestination;
  mutable bool replacementFailed{false};

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
    ++calls;
    if (failExtraction)
      return {termify::platform::ProcessCompletion::Exited, 1};
    const auto destination = argumentAfter(request.arguments, "-C");
    if (!destination)
      return {termify::platform::ProcessCompletion::Exited, 2};
    auto name =
        std::filesystem::path(request.arguments.at(1)).filename().string();
    name.resize(name.size() - std::string(".tar.gz").size());
    writeFile(std::filesystem::path(*destination) / name / "termify",
              termifyContents);
    if (!omitDownloader)
      writeFile(std::filesystem::path(*destination) / name / "yt-dlp",
                downloaderContents);
    return {termify::platform::ProcessCompletion::Exited, 0};
  }

  std::string termifyContents{"new-termify"};
  std::string downloaderContents{"new-downloader"};
  bool failExtraction{false};
  bool omitDownloader{false};
  unsigned calls{0};
};

UpdateEnvironment appImageEnvironment(const std::filesystem::path &path) {
  return {ReleasePlatform::Linux, ReleaseArchitecture::X86_64,
          InstallationKind::AppImage, path};
}

PreparedUpdate appImageUpdate(const std::filesystem::path &path) {
  return {Version{0, 8, 0},
          "termify-v0.8.0-linux-x86_64.AppImage",
          path,
          {},
          InstallationKind::AppImage};
}

UpdateEnvironment nativeEnvironment(const std::filesystem::path &path) {
  return {ReleasePlatform::Linux, ReleaseArchitecture::X86_64,
          InstallationKind::UserLocal, path};
}

PreparedUpdate nativeUpdate(const std::filesystem::path &path) {
  return {Version{0, 8, 0},
          "termify-v0.8.0-linux-x86_64.tar.gz",
          path,
          {},
          InstallationKind::UserLocal};
}

std::size_t rollbackStateCount(const std::filesystem::path &root) {
  const auto updates = root / "updates";
  if (!std::filesystem::exists(updates))
    return 0;
  std::size_t result = 0;
  for (const auto &entry : std::filesystem::directory_iterator(updates)) {
    if (entry.path().filename().string().starts_with("rollback-state-"))
      ++result;
  }
  return result;
}

std::filesystem::path rollbackStatePath(const std::filesystem::path &root) {
  for (const auto &entry :
       std::filesystem::directory_iterator(root / "updates")) {
    if (entry.path().filename().string().starts_with("rollback-state-"))
      return entry.path();
  }
  return {};
}

} // namespace

int main() {
  const auto root =
      std::filesystem::temp_directory_path() / "termify-update-installer-tests";
  std::filesystem::remove_all(root);
  std::filesystem::create_directories(root);
  TestFileSystem files(root);
  TestProcessRunner processes;
  UpdateInstaller installer(processes, files);

  const auto appImage = root / "Termify.AppImage";
  const auto appImageArtifact = root / "new.AppImage";
  writeFile(appImage, "old-appimage");
  writeFile(appImageArtifact, "new-appimage");
  installer.Install(appImageUpdate(appImageArtifact),
                    appImageEnvironment(appImage));
  CHECK(readFile(appImage) == "new-appimage");
  CHECK(rollbackStateCount(root) == 1);
  const auto statePath = rollbackStatePath(root);
  auto pendingState = readFile(statePath);
  const auto phase = pendingState.find("\nready\n");
  CHECK(phase != std::string::npos);
  pendingState.replace(phase, std::string("\nready\n").size(), "\npending\n");
  writeFile(statePath, pendingState);
  CHECK(throws([&] {
    installer.Install(appImageUpdate(appImageArtifact),
                      appImageEnvironment(appImage));
  }));
  CHECK(readFile(appImage) == "new-appimage");
  installer.Rollback(appImageEnvironment(appImage));
  CHECK(readFile(appImage) == "old-appimage");
  CHECK(rollbackStateCount(root) == 0);
  CHECK(throws([&] { installer.Rollback(appImageEnvironment(appImage)); }));

  writeFile(appImageArtifact, "second-appimage");
  installer.Install(appImageUpdate(appImageArtifact),
                    appImageEnvironment(appImage));
  writeFile(appImageArtifact, "third-appimage");
  installer.Install(appImageUpdate(appImageArtifact),
                    appImageEnvironment(appImage));
  CHECK(readFile(appImage) == "third-appimage");
  installer.Rollback(appImageEnvironment(appImage));
  CHECK(readFile(appImage) == "second-appimage");

  std::filesystem::remove_all(root);
  std::filesystem::create_directories(root);
  const auto termify = root / "prefix/bin/termify";
  const auto downloader = root / "prefix/lib/termify/yt-dlp";
  const auto archive = root / "termify-v0.8.0-linux-x86_64.tar.gz";
  writeFile(termify, "old-termify");
  writeFile(downloader, "old-downloader");
  writeFile(archive, "verified-archive");
  installer.Install(nativeUpdate(archive), nativeEnvironment(termify));
  CHECK(processes.calls == 1);
  CHECK(readFile(termify) == "new-termify");
  CHECK(readFile(downloader) == "new-downloader");
  installer.Rollback(nativeEnvironment(termify));
  CHECK(readFile(termify) == "old-termify");
  CHECK(readFile(downloader) == "old-downloader");

  writeFile(archive, "verified-archive");
  files.failOnceDestination = termify;
  files.replacementFailed = false;
  CHECK(throws([&] {
    installer.Install(nativeUpdate(archive), nativeEnvironment(termify));
  }));
  CHECK(readFile(termify) == "old-termify");
  CHECK(readFile(downloader) == "old-downloader");
  CHECK(rollbackStateCount(root) == 0);

  files.failOnceDestination.reset();
  processes.omitDownloader = true;
  CHECK(throws([&] {
    installer.Install(nativeUpdate(archive), nativeEnvironment(termify));
  }));
  CHECK(readFile(termify) == "old-termify");
  CHECK(readFile(downloader) == "old-downloader");

  const UpdateEnvironment managed{ReleasePlatform::Linux,
                                  ReleaseArchitecture::X86_64,
                                  InstallationKind::PackageManaged, termify};
  CHECK(throws([&] { installer.Install(nativeUpdate(archive), managed); }));
  CHECK(throws([&] { installer.Rollback(managed); }));

  std::filesystem::remove_all(root);
  std::cout << "Update installer tests passed.\n";
  return 0;
}
