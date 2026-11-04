#include "Core/UpdateChecker.hpp"

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

using termify::core::UpdateChecker;
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
  std::filesystem::path ExecutableDirectory() const override { return "/fake"; }
  std::filesystem::path TemporaryDirectory() const override {
    return std::filesystem::temp_directory_path();
  }

  std::optional<std::filesystem::path>
  FindExecutable(const std::string &name,
                 const std::vector<std::filesystem::path> &) const override {
    if (provideCurl && name == "curl")
      return "/fake/curl";
    return std::nullopt;
  }

  std::filesystem::path TemporaryStem(const std::string &) override {
    return std::filesystem::temp_directory_path() / "unused";
  }

  std::filesystem::path
  UserDataDirectory(const std::string &) const override {
    return std::filesystem::temp_directory_path();
  }

  std::filesystem::path
  UserCacheDirectory(const std::string &) const override {
    return std::filesystem::temp_directory_path();
  }

  std::shared_ptr<termify::platform::FileLease>
  TryLock(const std::filesystem::path &,
          termify::platform::FileLockMode) const override {
    return std::make_shared<TestLease>();
  }

  termify::platform::LockedFile
  CreateLockedFile(const std::filesystem::path &directory,
                   const std::string &prefix) const override {
    const auto path = directory /
                      (prefix + "-test-" + std::to_string(sequence++));
    std::ofstream(path).close();
    return {path, std::make_shared<TestLease>()};
  }

  bool provideCurl{true};

private:
  mutable unsigned sequence{0};
};

class TestProcessRunner final : public termify::platform::ProcessRunner {
public:
  termify::platform::ProcessResult
  Run(const termify::platform::ProcessRequest &request,
      const std::atomic_bool *,
      std::optional<std::chrono::milliseconds> timeout) override {
    called = true;
    executable = request.executable;
    arguments = request.arguments;
    requestedTimeout = timeout;
    if (request.standardOutput)
      std::ofstream(*request.standardOutput) << response;
    return result;
  }

  bool called{false};
  std::filesystem::path executable;
  std::vector<std::string> arguments;
  std::optional<std::chrono::milliseconds> requestedTimeout;
  std::string response =
      "https://github.com/segfaultcafe/Termify/releases/tag/v0.8.0\n";
  termify::platform::ProcessResult result{
      termify::platform::ProcessCompletion::Exited, 0};
};

bool contains(const std::vector<std::string> &values,
              const std::string &expected) {
  return std::find(values.begin(), values.end(), expected) != values.end();
}

} // namespace

int main() {
  CHECK(Version::Parse("0.7.0") == Version(0, 7, 0));
  CHECK(Version::Parse("v12.34.56") == Version(12, 34, 56));
  CHECK(Version::Parse("1.10.0") > Version::Parse("1.9.99"));
  CHECK(throws([] { Version::Parse("1.2"); }));
  CHECK(throws([] { Version::Parse("1.2.3.4"); }));
  CHECK(throws([] { Version::Parse("1.2.3-rc1"); }));
  CHECK(throws([] { Version::Parse("v1.two.3"); }));

  const auto available = UpdateChecker::Evaluate(
      "0.7.0",
      "https://github.com/segfaultcafe/Termify/releases/tag/v0.8.0");
  CHECK(available.state == UpdateState::Available);
  CHECK(available.current == Version(0, 7, 0));
  CHECK(available.latest == Version(0, 8, 0));
  CHECK(UpdateChecker::Format(available) ==
        "Termify v0.8.0 is available (installed: v0.7.0).\n"
        "Release: https://github.com/segfaultcafe/Termify/releases/tag/"
        "v0.8.0\n");

  const auto current = UpdateChecker::Evaluate(
      "0.8.0",
      "https://github.com/segfaultcafe/Termify/releases/tag/v0.8.0");
  CHECK(current.state == UpdateState::UpToDate);
  CHECK(UpdateChecker::Format(current) ==
        "Termify v0.8.0 is up to date.\n");

  const auto development = UpdateChecker::Evaluate(
      "0.9.0",
      "https://github.com/segfaultcafe/Termify/releases/tag/v0.8.0");
  CHECK(development.state == UpdateState::DevelopmentBuild);
  CHECK(UpdateChecker::Format(development) ==
        "Termify v0.9.0 is newer than the latest stable release (v0.8.0).\n");

  CHECK(throws([] {
    UpdateChecker::Evaluate(
        "0.7.0",
        "https://example.com/segfaultcafe/Termify/releases/tag/v99.0.0");
  }));
  CHECK(throws([] {
    UpdateChecker::Evaluate(
        "0.7.0",
        "https://github.com/segfaultcafe/Termify/releases/tag/v0.8.0?bad=1");
  }));
  CHECK(throws([] {
    UpdateChecker::Evaluate(
        "0.7.0",
        "https://github.com/segfaultcafe/Termify/releases/tag/v0.8.0-rc1");
  }));

  TestProcessRunner processes;
  TestFileSystem files;
  UpdateChecker checker(processes, files);
  const auto fetched = checker.Check("0.7.0");
  CHECK(fetched.state == UpdateState::Available);
  CHECK(processes.called);
  CHECK(processes.executable == "/fake/curl");
  CHECK(contains(processes.arguments, "--proto"));
  CHECK(contains(processes.arguments, "--proto-redir"));
  CHECK(contains(processes.arguments, "=https"));
  CHECK(contains(processes.arguments, "--max-time"));
  CHECK(contains(processes.arguments, "--head"));
  CHECK(processes.requestedTimeout == std::chrono::seconds(20));

  files.provideCurl = false;
  CHECK(throws([&] { checker.Check("0.7.0"); }));

  files.provideCurl = true;
  processes.result = {termify::platform::ProcessCompletion::TimedOut, -1};
  CHECK(throws([&] { checker.Check("0.7.0"); }));

  std::cout << "Update checker tests passed.\n";
  return 0;
}
