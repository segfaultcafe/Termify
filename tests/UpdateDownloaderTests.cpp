#include "Core/Sha256.hpp"
#include "Core/UpdateDownloader.hpp"

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

using termify::core::Sha256;
using termify::core::Sha256File;
using termify::core::UpdateDownloader;
using termify::core::UpdateState;
using termify::core::Version;

#define CHECK(condition)                                                       \
  do {                                                                         \
    if (!(condition)) {                                                        \
      std::cerr << "Check failed at line " << __LINE__ << ": " #condition    \
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

class TestLease final : public termify::platform::FileLease {};

class TestFileSystem final : public termify::platform::FileSystem {
public:
  explicit TestFileSystem(std::filesystem::path rootValue)
      : root(std::move(rootValue)) {}

  std::filesystem::path ExecutableDirectory() const override { return root; }
  std::filesystem::path TemporaryDirectory() const override { return root; }

  std::optional<std::filesystem::path>
  FindExecutable(const std::string &name,
                 const std::vector<std::filesystem::path> &) const override {
    if (name == "curl" && provideCurl)
      return "/fake/curl";
    if (name == "gh" && provideGithub)
      return "/fake/gh";
    return std::nullopt;
  }

  std::filesystem::path TemporaryStem(const std::string &prefix) override {
    return root / prefix;
  }
  std::filesystem::path
  UserDataDirectory(const std::string &) const override {
    return root;
  }
  std::filesystem::path
  UserCacheDirectory(const std::string &) const override {
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
  void ReplaceFileAtomically(const std::filesystem::path &,
                             const std::filesystem::path &,
                             bool) const override {
    throw std::runtime_error("unused");
  }

  std::filesystem::path root;
  bool provideCurl{true};
  bool provideGithub{true};

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
      std::optional<std::chrono::milliseconds> timeout) override {
    executables.push_back(request.executable);
    arguments.push_back(request.arguments);
    timeouts.push_back(timeout);
    if (request.executable == "/fake/gh") {
      ++attestations;
      if (failAttestation)
        return {termify::platform::ProcessCompletion::Exited, 1};
      return {termify::platform::ProcessCompletion::Exited, 0};
    }
    const auto output = argumentAfter(request.arguments, "--output");
    if (!output)
      return {termify::platform::ProcessCompletion::Exited, 2};
    const auto &url = request.arguments.back();
    if (url.ends_with("/SHA256SUMS"))
      std::ofstream(*output) << manifest;
    else
      std::ofstream(*output, std::ios::binary) << artifact;
    return {termify::platform::ProcessCompletion::Exited, 0};
  }

  std::string artifact{"abc"};
  std::string manifest =
      "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad  "
      "termify-v0.8.0-linux-x86_64.AppImage\n";
  bool failAttestation{false};
  unsigned attestations{0};
  std::vector<std::filesystem::path> executables;
  std::vector<std::vector<std::string>> arguments;
  std::vector<std::optional<std::chrono::milliseconds>> timeouts;
};

termify::platform::UpdateEnvironment linuxAppImage() {
  return {termify::platform::ReleasePlatform::Linux,
          termify::platform::ReleaseArchitecture::X86_64,
          termify::platform::InstallationKind::AppImage, "/tmp/termify.AppImage"};
}

} // namespace

int main() {
  CHECK(Sha256("") ==
        "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
  CHECK(Sha256("abc") ==
        "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
  CHECK(Sha256("The quick brown fox jumps over the lazy dog") ==
        "d7a8fbb307d7809469ca9abcb0082e4f8d5651e46d3cdb762d02d0bf37c9e592");
  CHECK(Sha256("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq") ==
        "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");

  const Version version{0, 8, 0};
  CHECK(UpdateDownloader::ArtifactName(version, linuxAppImage()) ==
        "termify-v0.8.0-linux-x86_64.AppImage");
  CHECK(UpdateDownloader::ArtifactName(
            version,
            {termify::platform::ReleasePlatform::Linux,
             termify::platform::ReleaseArchitecture::Arm64,
             termify::platform::InstallationKind::UserLocal, {}}) ==
        "termify-v0.8.0-linux-arm64.tar.gz");
  CHECK(UpdateDownloader::ArtifactName(
            version,
            {termify::platform::ReleasePlatform::MacOS,
             termify::platform::ReleaseArchitecture::X86_64,
             termify::platform::InstallationKind::UserLocal, {}}) ==
        "termify-v0.8.0-macos-x86_64.tar.gz");
  CHECK(throws([&] {
    UpdateDownloader::ArtifactName(
        version,
        {termify::platform::ReleasePlatform::Linux,
         termify::platform::ReleaseArchitecture::X86_64,
         termify::platform::InstallationKind::PackageManaged, {}});
  }));

  const auto checksum = UpdateDownloader::ExpectedChecksum(
      "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa  "
      "other\n"
      "BA7816BF8F01CFEA414140DE5DAE2223B00361A396177A9CB410FF61F20015AD  "
      "termify.AppImage\n",
      "termify.AppImage");
  CHECK(checksum ==
        "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
  CHECK(throws([] {
    UpdateDownloader::ExpectedChecksum("invalid\n", "termify.AppImage");
  }));
  CHECK(throws([] {
    UpdateDownloader::ExpectedChecksum(
        "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad  "
        "other\n",
        "termify.AppImage");
  }));
  CHECK(throws([] {
    UpdateDownloader::ExpectedChecksum(
        "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad  "
        "termify.AppImage\n"
        "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad  "
        "termify.AppImage\n",
        "termify.AppImage");
  }));

  const auto root = std::filesystem::temp_directory_path() /
                    "termify-update-downloader-tests";
  std::filesystem::remove_all(root);
  std::filesystem::create_directories(root);
  TestFileSystem files(root);
  TestProcessRunner processes;
  UpdateDownloader downloader(processes, files);
  const termify::core::UpdateCheckResult release{
      UpdateState::Available, Version{0, 7, 0}, version,
      "https://github.com/segfaultcafe/Termify/releases/tag/v0.8.0"};
  const auto prepared = downloader.Prepare(release, linuxAppImage());
  CHECK(prepared.artifactName ==
        "termify-v0.8.0-linux-x86_64.AppImage");
  CHECK(std::filesystem::exists(prepared.path));
  CHECK(std::filesystem::exists(prepared.manifestPath));
  CHECK(Sha256File(prepared.path) ==
        "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
  CHECK(processes.attestations == 2);
  CHECK(processes.executables.size() == 4);
  CHECK(std::find(processes.arguments[1].begin(), processes.arguments[1].end(),
                  "refs/tags/v0.8.0") != processes.arguments[1].end());
  CHECK(std::find(processes.arguments[1].begin(), processes.arguments[1].end(),
                  "--deny-self-hosted-runners") !=
        processes.arguments[1].end());
  CHECK(std::find(processes.arguments[0].begin(), processes.arguments[0].end(),
                  "=https") != processes.arguments[0].end());

  std::filesystem::remove_all(root);
  std::filesystem::create_directories(root);
  processes.manifest =
      "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa  "
      "termify-v0.8.0-linux-x86_64.AppImage\n";
  CHECK(throws([&] { downloader.Prepare(release, linuxAppImage()); }));
  CHECK(!std::filesystem::exists(
      root / "updates/v0.8.0/termify-v0.8.0-linux-x86_64.AppImage"));

  std::filesystem::remove_all(root);
  std::filesystem::create_directories(root);
  processes.failAttestation = false;
  files.provideGithub = false;
  CHECK(throws([&] { downloader.Prepare(release, linuxAppImage()); }));
  CHECK(!std::filesystem::exists(
      root / "updates/v0.8.0/termify-v0.8.0-linux-x86_64.AppImage"));

  std::filesystem::remove_all(root);
  std::filesystem::create_directories(root);
  processes.manifest =
      "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad  "
      "termify-v0.8.0-linux-x86_64.AppImage\n";
  files.provideGithub = true;
  processes.failAttestation = true;
  CHECK(throws([&] { downloader.Prepare(release, linuxAppImage()); }));
  CHECK(!std::filesystem::exists(
      root / "updates/v0.8.0/termify-v0.8.0-linux-x86_64.AppImage"));

  std::filesystem::remove_all(root);
  std::cout << "Update downloader tests passed.\n";
  return 0;
}
