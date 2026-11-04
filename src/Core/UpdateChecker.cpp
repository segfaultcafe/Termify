#include "UpdateChecker.hpp"

#include <charconv>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <system_error>
#include <utility>

namespace termify::core {
namespace {

constexpr std::string_view LATEST_RELEASE_URL =
    "https://github.com/segfaultcafe/Termify/releases/latest";
constexpr std::string_view RELEASE_TAG_PREFIX =
    "https://github.com/segfaultcafe/Termify/releases/tag/v";

class TemporaryFile {
public:
  explicit TemporaryFile(platform::LockedFile file)
      : path(std::move(file.path)), _lease(std::move(file.lease)) {}

  ~TemporaryFile() {
    std::error_code error;
    std::filesystem::remove(path, error);
  }

  TemporaryFile(const TemporaryFile &) = delete;
  TemporaryFile &operator=(const TemporaryFile &) = delete;

  std::filesystem::path path;

private:
  std::shared_ptr<platform::FileLease> _lease;
};

unsigned long long parsePart(std::string_view value) {
  if (value.empty())
    throw std::runtime_error("Invalid release version");
  unsigned long long result = 0;
  const auto parsed =
      std::from_chars(value.data(), value.data() + value.size(), result);
  if (parsed.ec != std::errc{} || parsed.ptr != value.data() + value.size())
    throw std::runtime_error("Invalid release version");
  return result;
}

std::string readSmallFile(const std::filesystem::path &path) {
  std::ifstream input(path, std::ios::binary);
  if (!input)
    throw std::runtime_error("Could not read the update response");
  std::string value(4097, '\0');
  input.read(value.data(), static_cast<std::streamsize>(value.size()));
  const auto count = input.gcount();
  if (count > 4096)
    throw std::runtime_error("The update response was unexpectedly large");
  value.resize(static_cast<std::size_t>(count));
  while (!value.empty() &&
         (value.back() == '\n' || value.back() == '\r' ||
          value.back() == ' ' || value.back() == '\t'))
    value.pop_back();
  return value;
}

} // namespace

Version Version::Parse(std::string_view value) {
  if (value.starts_with('v'))
    value.remove_prefix(1);
  const auto first = value.find('.');
  const auto second =
      first == std::string_view::npos ? first : value.find('.', first + 1);
  if (first == std::string_view::npos || second == std::string_view::npos ||
      value.find('.', second + 1) != std::string_view::npos)
    throw std::runtime_error("Invalid release version");
  return {parsePart(value.substr(0, first)),
          parsePart(value.substr(first + 1, second - first - 1)),
          parsePart(value.substr(second + 1))};
}

std::string Version::String() const {
  return std::to_string(major) + "." + std::to_string(minor) + "." +
         std::to_string(patch);
}

UpdateChecker::UpdateChecker(platform::ProcessRunner &processes,
                             platform::FileSystem &files)
    : _processes(processes), _files(files) {}

UpdateCheckResult UpdateChecker::Check(std::string_view currentVersion) const {
  return Evaluate(currentVersion, FetchLatestReleaseUrl());
}

UpdateCheckResult
UpdateChecker::Evaluate(std::string_view currentVersion,
                        std::string_view latestReleaseUrl) {
  if (!latestReleaseUrl.starts_with(RELEASE_TAG_PREFIX))
    throw std::runtime_error("GitHub returned an invalid Termify release URL");
  const auto tag = latestReleaseUrl.substr(RELEASE_TAG_PREFIX.size());
  if (tag.empty() || tag.find_first_of("/?#") != std::string_view::npos)
    throw std::runtime_error("GitHub returned an invalid Termify release URL");

  UpdateCheckResult result;
  result.current = Version::Parse(currentVersion);
  result.latest = Version::Parse(tag);
  result.releaseUrl = std::string(latestReleaseUrl);
  if (result.latest > result.current)
    result.state = UpdateState::Available;
  else if (result.latest < result.current)
    result.state = UpdateState::DevelopmentBuild;
  return result;
}

std::string UpdateChecker::Format(const UpdateCheckResult &result) {
  std::ostringstream output;
  switch (result.state) {
  case UpdateState::Available:
    output << "Termify v" << result.latest.String()
           << " is available (installed: v" << result.current.String()
           << ").\nRelease: " << result.releaseUrl << '\n';
    break;
  case UpdateState::DevelopmentBuild:
    output << "Termify v" << result.current.String()
           << " is newer than the latest stable release (v"
           << result.latest.String() << ").\n";
    break;
  case UpdateState::UpToDate:
    output << "Termify v" << result.current.String() << " is up to date.\n";
    break;
  }
  return output.str();
}

std::string UpdateChecker::FetchLatestReleaseUrl() const {
  const auto curl = _files.FindExecutable("curl", {});
  if (!curl)
    throw std::runtime_error(
        "curl is required to check for Termify updates");

  const auto temporaryDirectory = _files.TemporaryDirectory();
  TemporaryFile response(
      _files.CreateLockedFile(temporaryDirectory, "termify-update-response"));
  TemporaryFile body(
      _files.CreateLockedFile(temporaryDirectory, "termify-update-body"));

  const auto process = _processes.Run(
      {.executable = *curl,
       .arguments = {"--fail",
                     "--silent",
                     "--show-error",
                     "--location",
                     "--max-redirs",
                     "5",
                     "--connect-timeout",
                     "5",
                     "--max-time",
                     "15",
                     "--proto",
                     "=https",
                     "--proto-redir",
                     "=https",
                     "--head",
                     "--output",
                     body.path.string(),
                     "--write-out",
                     "%{url_effective}\\n",
                     std::string(LATEST_RELEASE_URL)},
       .standardOutput = response.path,
       .standardError = std::nullopt,
       .discardStandardError = true},
      nullptr, std::chrono::seconds(20));

  if (process.completion == platform::ProcessCompletion::TimedOut)
    throw std::runtime_error("The Termify update check timed out");
  if (!process.Succeeded())
    throw std::runtime_error("Could not contact the Termify release service");

  const auto url = readSmallFile(response.path);
  if (url.empty())
    throw std::runtime_error("The Termify release service returned no result");
  return url;
}

} // namespace termify::core
